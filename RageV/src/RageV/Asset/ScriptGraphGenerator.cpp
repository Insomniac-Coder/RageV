#include <rvpch.h>
#include "ScriptGraphGenerator.h"
#include "ScriptGraphSerializer.h"
#include "RageV/Core/Log.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace RageV::Assets
{
	namespace
	{
		// C# reserved words a file stem could plausibly collide with. Not the
		// whole list: an asset called `unsafe.rvgraph` is not a real risk, and
		// a table nobody can keep complete invites more confidence than it
		// deserves. These are the ones a person actually names a behaviour.
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
			"unchecked", "unsafe", "ushort", "using", "virtual", "void",
			"volatile", "while",
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
		// One pass of emission over one graph. A struct rather than a pile of
		// free functions because the expression walk needs the graph, the
		// issue list and the "did I use the text helper" flag, and threading
		// three references through eight functions is how one of them ends up
		// out of date.
		struct Emitter
		{
			const ScriptGraph& Graph;
			std::vector<GraphIssue>& Issues;
			bool UsedTextFloat = false;
			bool UsedTextBool = false;

			// Output pins read more than once, and the local each becomes.
			// Keyed node<<32|pin.
			std::unordered_map<uint64_t, std::string> Temps;
			std::vector<uint64_t> Used;

			static uint64_t Key(uint32_t node, uint32_t pin)
			{
				return ((uint64_t)node << 32) | pin;
			}

			// How deep the expression walk is, so a data cycle -- which the
			// exec-cycle check cannot see, because it only follows Exec pins --
			// stops instead of recursing until the stack goes.
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

			// The C# for whatever feeds `pin`, converted to `wanted`.
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

				// A value read twice is computed once, into a local declared
				// at the top of the method. The name is decided before the
				// declaration is written, which is what lets the statements be
				// emitted first and the prelude prepended after.
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
					{
						UsedTextBool = true;
						return "Text(" + expression + ")";
					}
					UsedTextFloat = true;
					return "Text(" + expression + ")";
				}

				return expression;
			}

			std::string Expression(const GraphNode& node, uint32_t pin)
			{
				switch (node.Type)
				{
					case GraphNodeType::LiteralBool:
						return node.Value.x != 0.0f ? "true" : "false";
					case GraphNodeType::LiteralFloat:
						return Number(node.Value.x);
					case GraphNodeType::LiteralVec3:
						// Straight to its text form: nothing in the v1 set
						// takes a Vec3, and the field API is text anyway.
						return "\"" + Number(node.Value.x) + " " + Number(node.Value.y)
							 + " " + Number(node.Value.z) + "\"";
					case GraphNodeType::LiteralString:
						return "\"" + Escape(node.Text) + "\"";

					case GraphNodeType::GetField:
					{
						const size_t dot = node.Text.find('.');
						const std::string component = node.Text.substr(0, dot);
						const std::string field = node.Text.substr(dot + 1);
						return "Entity.GetComponentField(\"" + Escape(component)
							 + "\", \"" + Escape(field) + "\")";
					}

					case GraphNodeType::Add:
					case GraphNodeType::Subtract:
					case GraphNodeType::Multiply:
					case GraphNodeType::Divide:
					{
						const char* op = node.Type == GraphNodeType::Add ? " + "
									   : node.Type == GraphNodeType::Subtract ? " - "
									   : node.Type == GraphNodeType::Multiply ? " * " : " / ";
						return "(" + Value(node.Id, 0, GraphPinType::Float) + op
							 + Value(node.Id, 1, GraphPinType::Float) + ")";
					}

					case GraphNodeType::Compare:
					{
						const int mode = Math::Clamp((int)node.Value.x, 0, 4);
						return "(" + Value(node.Id, 0, GraphPinType::Float) + " "
							 + kCompare[mode] + " "
							 + Value(node.Id, 1, GraphPinType::Float) + ")";
					}

					// An event's data outputs are the method's parameters.
					case GraphNodeType::OnTick:
						return "deltaTime";
					case GraphNodeType::OnCollisionEnter:
						return pin == 1 ? "collision.Other" : "collision.ImpactSpeed";

					default:
						Error(node.Id, std::string(GraphNodeDescOf(node.Type).Name)
							  + " cannot be used as a value");
						return "default";
				}
			}

			// The statements from `node` onward, following the Exec chain.
			void Statements(const GraphNode* node, std::string& out, int indent)
			{
				const std::string pad(indent, '\t');

				while (node)
				{
					const GraphNodeDesc& desc = GraphNodeDescOf(node->Type);

					switch (node->Type)
					{
						case GraphNodeType::SetField:
						{
							const size_t dot = node->Text.find('.');
							const std::string component = node->Text.substr(0, dot);
							const std::string field = node->Text.substr(dot + 1);
							out += pad + "Entity.SetComponentField(\"" + Escape(component)
								 + "\", \"" + Escape(field) + "\", "
								 + Value(node->Id, 1, GraphPinType::String) + ");\n";
							break;
						}

						case GraphNodeType::Log:
						{
							// The node's own text when nothing is wired in,
							// which is the literal fallback the validator
							// already allows for this pin.
							const std::string message = Graph.IsInputLinked(node->Id, 1)
								? Value(node->Id, 1, GraphPinType::String)
								: "\"" + Escape(node->Text) + "\"";
							out += pad + "Log.Info(" + message + ");\n";
							break;
						}

						case GraphNodeType::Branch:
						{
							// The condition already parenthesises itself when it
							// is an operator, and `if ((a > b))` is noise in a
							// file whose readability is the point.
							out += pad + "if (" + Unwrap(Value(node->Id, 1, GraphPinType::Bool))
								 + ")\n";
							out += pad + "{\n";
							Statements(Next(*node, 0), out, indent + 1);
							out += pad + "}\n";

							if (Next(*node, 1))
							{
								out += pad + "else\n" + pad + "{\n";
								Statements(Next(*node, 1), out, indent + 1);
								out += pad + "}\n";
							}
							// Both arms are terminal: a Branch has no exec
							// output past them, so nothing follows it.
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

					// Every statement node above has exactly one exec output,
					// at pin 0. Branch and Sequence returned.
					node = Next(*node, 0);
				}
			}

			// `var <name> = <expression>;` for each temp that was used, with
			// anything it depends on declared above it. Recursive rather than
			// sorted, because the dependency is discovered by emitting: a
			// temp's expression asks Value() for its inputs, which is what
			// records the temps *those* need.
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

				// Emitted into a scratch buffer first: writing the expression
				// is what discovers the temps it reads, and those have to be
				// declared above it.
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

		// Before emitting anything, not after: the point of the check is that
		// no file is written, and a generator that emits first and decides
		// later is one refactor away from writing it anyway.
		if (result.HasErrors())
			return result;

		Emitter emitter{ graph, result.Issues };

		// A pin read by two or more inputs earns a local. Counted over the
		// whole graph rather than per method: a pin read once in each of two
		// events is read once in each *method*, and hoisting it there would
		// declare a local used a single time.
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

			// An event's own outputs are already named -- `deltaTime`,
			// `collision.ImpactSpeed` -- so a local for one would be an alias.
			for (const auto& [key, count] : reads)
			{
				if (count < 2)
					continue;
				const GraphNode* node = graph.FindNode((uint32_t)(key >> 32));
				if (!node || GraphNodeDescOf(node->Type).IsEvent)
					continue;
				emitter.Temps[key] = "value" + std::to_string((uint32_t)(key >> 32));
			}
		}

		// Events in id order, so the file is deterministic (7bh, trap 1).
		std::vector<const GraphNode*> events;
		for (const GraphNode& node : graph.GetNodes())
		{
			if (GraphNodeDescOf(node.Type).IsEvent)
				events.push_back(&node);
		}
		std::sort(events.begin(), events.end(),
				  [](const GraphNode* a, const GraphNode* b) { return a->Id < b->Id; });

		std::string body;
		for (const GraphNode* event : events)
		{
			std::string signature;
			switch (event->Type)
			{
				case GraphNodeType::OnCreate:
					signature = "public override void OnCreate()";
					break;
				case GraphNodeType::OnTick:
					signature = "public override void OnTick(float deltaTime)";
					break;
				case GraphNodeType::OnCollisionEnter:
					signature = "public override void OnCollisionEnter(Collision collision)";
					break;
				default:
					continue;
			}

			// Statements first: writing them is what records which temps this
			// method actually reads. The prelude is then built from that and
			// put in front, which is why the names are chosen up front.
			emitter.Used.clear();
			std::string statements;
			emitter.Statements(emitter.Next(*event, 0), statements, 2);

			std::string prelude;
			std::vector<uint64_t> declared;
			const std::vector<uint64_t> used = emitter.Used;
			for (uint64_t key : used)
				emitter.DeclareTemp(key, declared, prelude);

			body += "\t" + signature + "\n\t{\n";
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
		out += "using RageV;\n\n";
		out += "public class " + className + " : Script\n{\n";
		out += body;

		// Emitted only where used, so a graph that needs neither does not
		// carry a helper somebody then wonders about.
		if (emitter.UsedTextFloat || emitter.UsedTextBool)
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

		// One trailing brace, and the body already ended with a blank line.
		while (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n')
			out.pop_back();
		out += "}\n";

		result.Source = std::move(out);
		result.Ok = true;
		return result;
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
			// A graph that has stopped generating must not leave last-good code
			// compiled in: the canvas would say "4 errors" while the game ran
			// the version from before them, which is the engine disagreeing
			// with the editor about what the project contains.
			std::error_code error;
			if (std::filesystem::exists(file, error))
			{
				std::filesystem::remove(file, error);
				RV_CORE_WARN("Script graph '{0}' no longer generates; removed {1}",
							 className, file.filename().string());
			}
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

		// Sorted, so a build's log reads the same way twice and a failure is
		// found in the same place.
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
			ScriptGraph graph;
			if (!ScriptGraphSerializer::Load(graph, path))
			{
				ok = false;
				continue;
			}

			const std::string className = path.stem().string();
			std::vector<GraphIssue> issues;
			if (GenerateToFile(graph, className, scriptsRoot, issues))
				continue;

			// One bad graph must not stop the others compiling, so this
			// records and carries on -- but it says which, and why, because a
			// script that silently stops existing is the worst way to find out.
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
