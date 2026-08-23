#include <rvpch.h>
#include "ScriptGraphGenerator.h"
#include "ScriptGraphSerializer.h"
#include "RageV/Core/Log.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <map>

namespace RageV::Assets
{
	namespace
	{
		// C# reserved words a file stem or a variable name could plausibly
		// collide with. Not the whole list: an asset called `unsafe.rvgraph` is
		// not a real risk, and a table nobody can keep complete invites more
		// confidence than it deserves. These are the ones a person actually
		// names a behaviour or a variable.
		const char* kReserved[] = {
			"abstract", "base", "bool", "break", "byte", "case", "catch", "char",
			"class", "const", "continue", "default", "delegate", "do", "double",
			"else", "enum", "event", "explicit", "extern", "false", "finally",
			"fixed", "float", "for", "foreach", "goto", "if", "implicit", "in",
			"int", "interface", "internal", "is", "lock", "long", "namespace",
			"new", "null", "object", "operator", "out", "override", "params",
			"private", "protected", "public", "readonly", "ref", "return",
			"sealed", "short", "sizeof", "static", "string", "struct", "switch",
			"this", "throw", "true", "try", "typeof", "uint", "ulong",
			"unchecked", "unsafe", "ushort", "using", "value", "var", "virtual",
			"void", "volatile", "while",
		};

		std::string Escape(const std::string& text)
		{
			std::string out;
			out.reserve(text.size() + 8);
			for (char c : text)
			{
				if (c == '\\' || c == '"')
					out += '\\';
				if (c == '\n')
				{
					out += "\\n";
					continue;
				}
				out += c;
			}
			return out;
		}

		// A float that round-trips and does not depend on a locale. `f` on the
		// end because these land in C# float expressions, where an unsuffixed
		// literal is a double and `float x = 1.5` does not compile.
		std::string Number(float value)
		{
			std::ostringstream out;
			out.imbue(std::locale::classic());
			out.precision(9);
			out << value;
			std::string text = out.str();
			if (text.find('.') == std::string::npos
				&& text.find('e') == std::string::npos
				&& text.find("inf") == std::string::npos
				&& text.find("nan") == std::string::npos)
				text += ".0";
			return text + "f";
		}

		const char* kCompare[] = { "<", "<=", "==", ">=", ">" };

		// One redundant layer of brackets off an expression that is already one
		// group. Balance-checked rather than assumed, so "(a) > (b)" -- which
		// is two groups and not one -- keeps both.
		std::string Unwrap(const std::string& text)
		{
			if (text.size() < 2 || text.front() != '(' || text.back() != ')')
				return text;

			int depth = 0;
			for (size_t i = 0; i < text.size(); i++)
			{
				depth += text[i] == '(' ? 1 : text[i] == ')' ? -1 : 0;
				if (depth == 0 && i + 1 < text.size())
					return text;
			}
			return text.substr(1, text.size() - 2);
		}

		// `{0}`, `{1}` ... replaced by the node's resolved inputs. The whole
		// reason ninety-nine nodes need about ninety-nine *lines* rather than
		// ninety-nine cases.
		std::string Format(const std::string& code,
						   const std::vector<std::string>& arguments)
		{
			std::string out;
			for (size_t i = 0; i < code.size(); i++)
			{
				if (code[i] != '{' || i + 2 >= code.size() || code[i + 2] != '}'
					|| !std::isdigit((unsigned char)code[i + 1]))
				{
					out += code[i];
					continue;
				}
				const size_t index = (size_t)(code[i + 1] - '0');
				out += index < arguments.size() ? arguments[index] : "default";
				i += 2;
			}
			return out;
		}
	}

	bool GraphGenerateResult::HasErrors() const
	{
		return std::any_of(Issues.begin(), Issues.end(), [](const GraphIssue& issue)
						   { return issue.Severity == GraphIssueSeverity::Error; });
	}

	bool ScriptGraphGenerator::IsIdentifier(const std::string& name)
	{
		if (name.empty() || (!std::isalpha((unsigned char)name[0]) && name[0] != '_'))
			return false;

		for (char c : name)
		{
			if (!std::isalnum((unsigned char)c) && c != '_')
				return false;
		}

		for (const char* word : kReserved)
		{
			if (name == word)
				return false;
		}
		return true;
	}

	namespace
	{
		// One pass of emission over one graph.
		struct Emitter
		{
			const ScriptGraph& Graph;
			std::vector<GraphIssue>& Issues;

			bool UsedTextFloat = false;
			bool UsedTextBool = false;
			bool UsedTextVector = false;

			// Output pins read more than once, and the local each becomes.
			std::unordered_map<uint64_t, std::string> Temps;
			std::vector<uint64_t> Used;

			// Variables the graph names, and the C# type each was declared at.
			// A field on the generated class, so it survives between events --
			// which is the entire point, and the thing v1 could not do.
			std::map<std::string, std::string> Variables;

			static uint64_t Key(uint32_t node, uint32_t pin)
			{
				return ((uint64_t)node << 32) | pin;
			}

			int Depth = 0;

			const GraphNode* SourceOf(uint32_t node, uint32_t pin, uint32_t& outPin) const
			{
				for (const GraphLink& link : Graph.GetLinks())
				{
					if (link.ToNode != node || link.ToPin != pin)
						continue;
					outPin = link.FromPin;
					return Graph.FindNode(link.FromNode);
				}
				return nullptr;
			}

			void Error(uint32_t node, std::string message)
			{
				Issues.push_back({ GraphIssueSeverity::Error, node, std::move(message) });
			}

			// A node that spawns something declares a local, and its Entity
			// output is that local. Named from the node id so two spawns in one
			// method cannot collide.
			static std::string SpawnLocal(uint32_t id)
			{
				return "spawned" + std::to_string(id);
			}

			std::string Value(uint32_t node, uint32_t pin, GraphPinType wanted)
			{
				uint32_t fromPin = 0;
				const GraphNode* from = SourceOf(node, pin, fromPin);
				if (!from)
					return "default";

				if (++Depth > 64)
				{
					Error(node, "the value chain into this node is circular");
					Depth--;
					return "default";
				}

				const GraphPinType have = GraphNodeDescOf(from->Type).Outputs[fromPin].Type;

				std::string expression;
				const auto temp = Temps.find(Key(from->Id, fromPin));
				if (temp != Temps.end())
				{
					expression = temp->second;
					if (std::find(Used.begin(), Used.end(), Key(from->Id, fromPin)) == Used.end())
						Used.push_back(Key(from->Id, fromPin));
				}
				else
				{
					expression = Expression(*from, fromPin);
				}
				Depth--;

				if (have == wanted)
					return expression;

				// The widening rule (ScriptGraph::PinAccepts): everything has a
				// text form, and text is what the engine's named field API
				// speaks. Invariant, so a machine with a comma decimal point
				// writes the same field value as one without.
				if (wanted == GraphPinType::String)
				{
					if (have == GraphPinType::Bool)
						UsedTextBool = true;
					else if (have == GraphPinType::Vec3)
						UsedTextVector = true;
					else
						UsedTextFloat = true;
					return "Text(" + expression + ")";
				}

				return expression;
			}

			// The inputs of a node, resolved. Exec pins resolve to nothing, so
			// a statement's format can index its data pins by their real
			// position and the table stays readable.
			std::vector<std::string> Arguments(const GraphNode& node)
			{
				const GraphNodeDesc& desc = GraphNodeDescOf(node.Type);
				std::vector<std::string> out;
				out.reserve(desc.Inputs.size());
				for (uint32_t i = 0; i < desc.Inputs.size(); i++)
				{
					out.push_back(desc.Inputs[i].Type == GraphPinType::Exec
								  ? std::string()
								  : Value(node.Id, i, desc.Inputs[i].Type));
				}
				return out;
			}

			std::string Expression(const GraphNode& node, uint32_t pin)
			{
				const GraphNodeDesc& desc = GraphNodeDescOf(node.Type);

				switch (desc.Emit)
				{
					case GraphEmit::Expression:
						return Format(desc.Code, Arguments(node));

					case GraphEmit::GetVariable:
						Variables[node.Text] = desc.Code;
						return node.Text;

					case GraphEmit::Event:
						// An event's data outputs are the method's parameters.
						if (node.Type == GraphNodeType::OnTick
							|| node.Type == GraphNodeType::OnFrame)
							return "deltaTime";
						switch (pin)
						{
							case 1: return "collision.Other";
							case 2: return "collision.ImpactSpeed";
							case 3: return "collision.Point";
							default: return "collision.Normal";
						}

					default:
						break;
				}

				switch (node.Type)
				{
					case GraphNodeType::LiteralBool:
						return node.Value.x != 0.0f ? "true" : "false";
					case GraphNodeType::LiteralFloat:
						return Number(node.Value.x);
					case GraphNodeType::LiteralVec3:
						return "new Vector3(" + Number(node.Value.x) + ", "
							 + Number(node.Value.y) + ", " + Number(node.Value.z) + ")";
					case GraphNodeType::LiteralString:
						return "\"" + Escape(node.Text) + "\"";

					case GraphNodeType::GetField:
					{
						const size_t dot = node.Text.find('.');
						return "Entity.GetComponentField(\""
							 + Escape(node.Text.substr(0, dot)) + "\", \""
							 + Escape(node.Text.substr(dot + 1)) + "\")";
					}

					case GraphNodeType::Compare:
					{
						const int mode = Math::Clamp((int)node.Value.x, 0, 4);
						std::vector<std::string> args = Arguments(node);
						return "(" + args[0] + " " + kCompare[mode] + " " + args[1] + ")";
					}

					case GraphNodeType::ForEachNumber:
					case GraphNodeType::ForEachEntity:
						// Element at pin 1, Index at pin 2.
						return pin == 1 ? "element" + std::to_string(node.Id)
							  : "(float)index" + std::to_string(node.Id);

					case GraphNodeType::ForLoop:
						// Index. A Float pin over an int counter, cast at the read.
						return "(float)index" + std::to_string(node.Id);

					case GraphNodeType::SpawnEntity:
					case GraphNodeType::SpawnPrefab:
						return SpawnLocal(node.Id);

					default:
						Error(node.Id, std::string(desc.Name)
							  + " cannot be used as a value");
						return "default";
				}
			}

			void Statements(const GraphNode* node, std::string& out, int indent)
			{
				const std::string pad(indent, '\t');

				while (node)
				{
					const GraphNodeDesc& desc = GraphNodeDescOf(node->Type);

					if (desc.Emit == GraphEmit::Statement)
					{
						std::vector<std::string> args = Arguments(*node);
						const std::string code = desc.Code;

						// `{N}.Property = ...` needs an *lvalue*, and most of what
						// feeds one here is not: Script.Entity is a property returning
						// a struct, and C# will not let you assign through the return
						// value of one. Spilling to a local fixes it and stays correct,
						// because an Entity is a handle -- the copy writes to the same
						// thing the original names.
						//
						// Found by *compiling* the output rather than reading it, which
						// is the whole reason check_graph runs dotnet.
						if (code.size() > 3 && code[0] == '{' && code[2] == '}'
							&& std::isdigit((unsigned char)code[1])
							&& code.find(" = ") != std::string::npos)
						{
							const size_t index = (size_t)(code[1] - '0');
							if (index < args.size())
							{
								const std::string local = "target" + std::to_string(node->Id);
								out += pad + "var " + local + " = " + args[index] + ";\n";
								args[index] = local;
							}
						}

						out += pad + Format(code, args) + "\n";
						node = Next(*node, 0);
						continue;
					}

					if (desc.Emit == GraphEmit::SetVariable)
					{
						Variables[node->Text] = desc.Code;
						out += pad + node->Text + " = " + Arguments(*node)[1] + ";\n";
						node = Next(*node, 0);
						continue;
					}

					switch (node->Type)
					{
						case GraphNodeType::SetField:
						{
							const size_t dot = node->Text.find('.');
							out += pad + "Entity.SetComponentField(\""
								 + Escape(node->Text.substr(0, dot)) + "\", \""
								 + Escape(node->Text.substr(dot + 1)) + "\", "
								 + Value(node->Id, 1, GraphPinType::String) + ");\n";
							break;
						}

						case GraphNodeType::SetFieldOn:
						{
							const size_t dot = node->Text.find('.');
							out += pad + Value(node->Id, 1, GraphPinType::Entity)
								 + ".SetComponentField(\""
								 + Escape(node->Text.substr(0, dot)) + "\", \""
								 + Escape(node->Text.substr(dot + 1)) + "\", "
								 + Value(node->Id, 2, GraphPinType::String) + ");\n";
							break;
						}

						case GraphNodeType::Log:
						case GraphNodeType::LogWarning:
						{
							// The node's own text when nothing is wired in,
							// which is the literal fallback the validator
							// allows for this pin.
							const std::string message = Graph.IsInputLinked(node->Id, 1)
								? Value(node->Id, 1, GraphPinType::String)
								: "\"" + Escape(node->Text) + "\"";
							out += pad + (node->Type == GraphNodeType::Log
										  ? "Log.Info(" : "Log.Warn(") + message + ");\n";
							break;
						}

						case GraphNodeType::SpawnEntity:
							out += pad + "var " + SpawnLocal(node->Id) + " = Entity.Spawn("
								 + Arguments(*node)[1] + ");\n";
							break;

						case GraphNodeType::SpawnPrefab:
							out += pad + "var " + SpawnLocal(node->Id)
								 + " = Entity.SpawnPrefab(" + Arguments(*node)[1] + ");\n";
							break;

					case GraphNodeType::ForEachNumber:
					case GraphNodeType::ForEachEntity:
					{
						// A for over Count rather than a C# foreach, because the node
						// offers an Index and foreach has none. Element is a local, so
						// the body reads it by name rather than indexing twice.
						const std::string counter = "index" + std::to_string(node->Id);
						const std::string list = Arguments(*node)[1];
						out += pad + "for (int " + counter + " = 0; " + counter + " < "
							+ list + ".Count; " + counter + "++)\n";
						out += pad + "{\n";
						out += pad + "\tvar element" + std::to_string(node->Id)
							+ " = " + list + "[" + counter + "];\n";
						Statements(Next(*node, 0), out, indent + 1);
						out += pad + "}\n";
						node = Next(*node, 3);
						continue;
					}

						case GraphNodeType::ForLoop:
						{
							// Body then Completed, in the Blueprint sense. The counter is an
							// int because that is what a loop counts with; Index is a Float
							// pin, so it casts at the read rather than accumulating error in
							// a float counter.
							const std::string counter = "index" + std::to_string(node->Id);
							out += pad + "for (int " + counter + " = 0; " + counter + " < (int)("
								+ Arguments(*node)[1] + "); " + counter + "++)\n";
							out += pad + "{\n";
							Statements(Next(*node, 0), out, indent + 1);
							out += pad + "}\n";
							node = Next(*node, 2);
							continue;
						}

						case GraphNodeType::WhileLoop:
						{
							// A guard, and it says so when it trips. A graph author can write
							// a condition that never goes false, and an unbounded while in a
							// script means the editor stops responding with no clue why --
							// which is a worse failure than the loop ending early and warning.
							const std::string guard = "guard" + std::to_string(node->Id);
							out += pad + "int " + guard + " = 0;\n";
							out += pad + "while ("
								+ Unwrap(Value(node->Id, 1, GraphPinType::Bool)) + ")\n";
							out += pad + "{\n";
							out += pad + "\t" + "if (++" + guard + " > 1000000)\n";
							out += pad + "\t" + "{\n";
							out += pad + "\t" + "\t" + "Log.Warn(\"While Loop ran a million times and was stopped; its condition never went false.\");\n";
							out += pad + "\t" + "\t" + "break;\n";
							out += pad + "\t" + "}\n";
							Statements(Next(*node, 0), out, indent + 1);
							out += pad + "}\n";
							node = Next(*node, 1);
							continue;
						}

						case GraphNodeType::BreakLoop:
							// Terminal: nothing after a break in the same chain can run, and
							// the validator has already refused one outside a loop body.
							out += pad + "break;\n";
							return;

						case GraphNodeType::CallFunction:
							out += pad + node->Text + "();\n";
							break;

						case GraphNodeType::Branch:
						{
							out += pad + "if ("
								 + Unwrap(Value(node->Id, 1, GraphPinType::Bool)) + ")\n";
							out += pad + "{\n";
							Statements(Next(*node, 0), out, indent + 1);
							out += pad + "}\n";

							if (Next(*node, 1))
							{
								out += pad + "else\n" + pad + "{\n";
								Statements(Next(*node, 1), out, indent + 1);
								out += pad + "}\n";
							}
							return;
						}

						case GraphNodeType::Sequence:
							Statements(Next(*node, 0), out, indent);
							Statements(Next(*node, 1), out, indent);
							return;

						default:
							Error(node->Id, std::string(desc.Name)
								  + " is not a statement and cannot be run");
							return;
					}

					node = Next(*node, 0);
				}
			}

			void DeclareTemp(uint64_t key, std::vector<uint64_t>& done, std::string& out)
			{
				if (std::find(done.begin(), done.end(), key) != done.end())
					return;
				done.push_back(key);

				const uint32_t id = (uint32_t)(key >> 32);
				const uint32_t pin = (uint32_t)(key & 0xFFFFFFFFu);
				const GraphNode* node = Graph.FindNode(id);
				if (!node)
					return;

				const size_t before = Used.size();
				const std::string expression = Expression(*node, pin);
				for (size_t i = before; i < Used.size(); i++)
					DeclareTemp(Used[i], done, out);

				out += "\t\tvar " + Temps[key] + " = " + expression + ";\n";
			}

			const GraphNode* Next(const GraphNode& node, uint32_t pin) const
			{
				for (const GraphLink& link : Graph.GetLinks())
				{
					if (link.FromNode == node.Id && link.FromPin == pin)
						return Graph.FindNode(link.ToNode);
				}
				return nullptr;
			}
		};
	}

	GraphGenerateResult ScriptGraphGenerator::Generate(const ScriptGraph& graph,
													   const std::string& className)
	{
		GraphGenerateResult result;
		result.Issues = graph.Validate();

		if (!IsIdentifier(className))
		{
			result.Issues.insert(result.Issues.begin(),
								 { GraphIssueSeverity::Error, 0,
								   "'" + className + "' cannot be a C# type name; rename the "
								   "file to letters, digits and underscores" });
		}

		// A variable is a C# field, so its name has to be one. Checked here
		// rather than left to the compiler: "my count" would otherwise produce
		// a generated file that does not build, and the error would point at
		// code nobody wrote.
		for (const GraphNode& node : graph.GetNodes())
		{
			const GraphEmit emit = GraphNodeDescOf(node.Type).Emit;
			if (emit != GraphEmit::GetVariable && emit != GraphEmit::SetVariable)
				continue;
			if (node.Text.empty())
				result.Issues.push_back({ GraphIssueSeverity::Error, node.Id,
										  "this variable has no name" });
			else if (!IsIdentifier(node.Text))
				result.Issues.push_back({ GraphIssueSeverity::Error, node.Id,
										  "'" + node.Text + "' cannot be a variable name; "
										  "use letters, digits and underscores" });
		}

		if (result.HasErrors())
			return result;

		Emitter emitter{ graph, result.Issues };

		{
			std::unordered_map<uint64_t, int> reads;
			for (const GraphLink& link : graph.GetLinks())
			{
				const GraphNode* from = graph.FindNode(link.FromNode);
				if (!from)
					continue;
				const GraphNodeDesc& desc = GraphNodeDescOf(from->Type);
				if (link.FromPin >= desc.Outputs.size()
					|| desc.Outputs[link.FromPin].Type == GraphPinType::Exec)
					continue;
				reads[Emitter::Key(link.FromNode, link.FromPin)]++;
			}

			for (const auto& [key, count] : reads)
			{
				if (count < 2)
					continue;
				const GraphNode* node = graph.FindNode((uint32_t)(key >> 32));
				if (!node)
					continue;
				const GraphEmit emit = GraphNodeDescOf(node->Type).Emit;

				// An event's outputs are already named, a variable already is
				// a name, and a spawn already declares its own local. A local
				// for any of those would be an alias.
				if (emit == GraphEmit::Event || emit == GraphEmit::GetVariable
					|| node->Type == GraphNodeType::SpawnEntity
					|| node->Type == GraphNodeType::SpawnPrefab)
					continue;
				emitter.Temps[key] = "value" + std::to_string((uint32_t)(key >> 32));
			}
		}

		std::vector<const GraphNode*> events;
		for (const GraphNode& node : graph.GetNodes())
		{
			if (GraphNodeDescOf(node.Type).IsEvent)
				events.push_back(&node);
		}
		std::sort(events.begin(), events.end(),
				  [](const GraphNode* a, const GraphNode* b) { return a->Id < b->Id; });

		// Functions are roots too, and become private methods. Parameterless
		// by design (7bh): pins come from a node *type* here, so a per-instance
		// signature is not expressible -- and the graph's variables are already
		// class fields that a function can read and write, which is how a
		// caller hands one anything.
		std::vector<const GraphNode*> functions;
		for (const GraphNode& node : graph.GetNodes())
		{
			if (node.Type == GraphNodeType::FunctionEntry)
				functions.push_back(&node);
		}
		std::sort(functions.begin(), functions.end(),
				  [](const GraphNode* a, const GraphNode* b) { return a->Id < b->Id; });

		std::string body;
		for (const GraphNode* event : events)
		{
			emitter.Used.clear();
			std::string statements;
			emitter.Statements(emitter.Next(*event, 0), statements, 2);

			std::string prelude;
			std::vector<uint64_t> declared;
			const std::vector<uint64_t> used = emitter.Used;
			for (uint64_t key : used)
				emitter.DeclareTemp(key, declared, prelude);

			body += std::string("\t") + GraphNodeDescOf(event->Type).Code + "\n\t{\n";
			body += prelude;
			if (!prelude.empty() && !statements.empty())
				body += "\n";
			body += statements;
			body += "\t}\n\n";
		}

		for (const GraphNode* function : functions)
		{
			emitter.Used.clear();
			std::string statements;
			emitter.Statements(emitter.Next(*function, 0), statements, 2);

			std::string prelude;
			std::vector<uint64_t> declared;
			const std::vector<uint64_t> used = emitter.Used;
			for (uint64_t key : used)
				emitter.DeclareTemp(key, declared, prelude);

			// Same question for a function, and the same default: private
			// unless the graph says otherwise.
			const GraphFunction* declaredFunction = graph.FindFunction(function->Text);
			const bool functionPublic = declaredFunction && declaredFunction->Public;

			body += std::string("\t") + (functionPublic ? "public" : "private")
				  + " void " + function->Text + "()\n\t{\n";
			body += prelude;
			if (!prelude.empty() && !statements.empty())
				body += "\n";
			body += statements;
			body += "\t}\n\n";
		}

		if (emitter.Issues.size() != result.Issues.size())
			result.Issues = emitter.Issues;
		if (result.HasErrors())
			return result;

		std::string out;
		out += "// Generated from " + className + ".rvgraph. Do not edit.\n";
		out += "//\n";
		out += "// Rewritten whenever the graph is saved or the scripts are built, so an\n";
		out += "// edit here survives exactly until the next one. Change the graph.\n";
		out += "// ENGINE-NOTES 7bh.\n\n";
		// Only when something needs it: an unused using is noise in a file
		// somebody is meant to be able to read.
		bool containers = false;
		for (const auto& entry : emitter.Variables)
			containers = containers || entry.second.find('<') != std::string::npos;

		out += "using RageV;\n";
		if (containers)
			out += "using System.Collections.Generic;\n";
		out += "\n";
		out += "public class " + className + " : Script\n{\n";

		// Variables first: they are the class's state, and a reader looking for
		// what a script *remembers* should not have to find it among methods.
		if (!emitter.Variables.empty())
		{
			out += "\t// The graph's variables. Fields rather than locals, because\n";
			out += "\t// that is what makes them survive between one event and the next.\n";
			for (const auto& [name, type] : emitter.Variables)
			{
				// What the graph says about this one, if it says anything. A
				// variable nothing has been said about keeps the shape it had
				// before declarations existed: private, and visible.
				const GraphVariable* declared = graph.FindVariable(name);
				const bool isPublic = declared && declared->Public;
				const bool shown = !declared || declared->ShowInEditor;

				// The inspector reflects every instance field, private ones
				// included, so hiding needs a marker rather than a modifier.
				if (!shown)
					out += "\t[HideInEditor]\n";

				// A container field has to be constructed. Left null, the first
				// Add throws, and the stack trace lands in generated code rather
				// than on the node that asked for it.
				const bool container = type.find('<') != std::string::npos;
				out += std::string("\t") + (isPublic ? "public " : "private ")
					+ type + " " + name
					+ (container ? " = new " + type + "();" : ";") + "\n";
			}
			out += "\n";
		}

		out += body;

		if (emitter.UsedTextFloat || emitter.UsedTextBool || emitter.UsedTextVector)
		{
			out += "\t// The engine's named field API is text, and these are the forms it\n";
			out += "\t// parses. Invariant: a machine with a comma decimal point has to\n";
			out += "\t// write the same field value as one without.\n";
		}
		if (emitter.UsedTextFloat)
		{
			out += "\tprivate static string Text(float value) =>\n";
			out += "\t\tvalue.ToString(System.Globalization.CultureInfo.InvariantCulture);\n\n";
		}
		if (emitter.UsedTextBool)
			out += "\tprivate static string Text(bool value) => value ? \"true\" : \"false\";\n\n";
		if (emitter.UsedTextVector)
		{
			out += "\tprivate static string Text(Vector3 value) =>\n";
			out += "\t\tText(value.X) + \" \" + Text(value.Y) + \" \" + Text(value.Z);\n\n";
		}

		while (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n')
			out.pop_back();
		out += "}\n";

		result.Source = std::move(out);
		result.Ok = true;
		return result;
	}

	namespace
	{
		// A graph that has stopped generating must not leave last-good code
		// compiled in: the canvas would say "4 errors" while the game ran the
		// version from before them. Both callers below need it -- one for a
		// graph that fails to validate, one for a graph that will not load --
		// so the rule lives here rather than twice.
		void RemoveStaleScript(const std::string& className,
							   const std::filesystem::path& scriptsRoot,
							   const char* because)
		{
			const std::filesystem::path file =
				scriptsRoot / "Generated" / (className + ".g.cs");

			std::error_code error;
			if (!std::filesystem::exists(file, error))
				return;

			std::filesystem::remove(file, error);
			RV_CORE_WARN("Script graph '{0}' {1}; removed {2}",
						 className, because, file.filename().string());
		}
	}

	bool ScriptGraphGenerator::GenerateToFile(const ScriptGraph& graph,
											  const std::string& className,
											  const std::filesystem::path& scriptsRoot,
											  std::vector<GraphIssue>& outIssues)
	{
		const GraphGenerateResult result = Generate(graph, className);
		outIssues = result.Issues;

		const std::filesystem::path directory = scriptsRoot / "Generated";
		const std::filesystem::path file = directory / (className + ".g.cs");

		if (!result.Ok)
		{
			RemoveStaleScript(className, scriptsRoot, "no longer generates");
			return false;
		}

		std::error_code error;
		std::filesystem::create_directories(directory, error);

		// Only when it differs. Rewriting an identical file touches its
		// timestamp, which makes MSBuild rebuild the assembly, which restarts
		// the scene -- every save, for nothing.
		if (std::ifstream existing(file); existing)
		{
			const std::string previous((std::istreambuf_iterator<char>(existing)),
									   std::istreambuf_iterator<char>());
			if (previous == result.Source)
				return true;
		}

		std::ofstream out(file, std::ios::binary);
		if (!out)
		{
			RV_CORE_ERROR("Could not write {0}", file.string());
			return false;
		}
		out << result.Source;
		return true;
	}

	bool ScriptGraphGenerator::GenerateAll(const std::filesystem::path& assetsRoot,
										   const std::filesystem::path& scriptsRoot)
	{
		std::error_code error;
		if (!std::filesystem::exists(assetsRoot, error))
			return true;

		std::vector<std::filesystem::path> files;
		for (const std::filesystem::directory_entry& entry :
			 std::filesystem::recursive_directory_iterator(assetsRoot, error))
		{
			if (entry.is_regular_file(error) && entry.path().extension() == ".rvgraph")
				files.push_back(entry.path());
		}
		std::sort(files.begin(), files.end());

		bool ok = true;
		for (const std::filesystem::path& path : files)
		{
			// **A graph that will not load is a failure with output to clean
			// up, not a file to skip** (10.10). The loader refuses a graph
			// naming a node type this build does not have, and it used to drop
			// the node instead -- which meant this loop generated a shortened
			// script over the top of the real one. Refusing fixes that half;
			// this fixes the other, or the *previous* script survives and the
			// game runs behaviour the graph no longer describes.
			// No out-param: `Refuse` has already put the sentence in the log,
			// which is where a build reads it. The editor asks for the string
			// because a console is not where somebody who double-clicked a
			// file is looking.
			ScriptGraph graph;
			if (!ScriptGraphSerializer::Load(graph, path))
			{
				ok = false;
				RemoveStaleScript(path.stem().string(), scriptsRoot,
								  "will not load");
				continue;
			}

			const std::string className = path.stem().string();
			std::vector<GraphIssue> issues;
			if (GenerateToFile(graph, className, scriptsRoot, issues))
				continue;

			ok = false;
			RV_CORE_ERROR("Script graph '{0}' did not generate:", className);
			for (const GraphIssue& issue : issues)
			{
				if (issue.Severity == GraphIssueSeverity::Error)
					RV_CORE_ERROR("  {0}", issue.Message);
			}
		}
		return ok;
	}
}
