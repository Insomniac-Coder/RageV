#include <rvpch.h>
#include "ScriptGen.h"

#include <sstream>

// The scanner. Three stages, each honest about what it is:
//
//   1. Clean  -- comments, string literals and preprocessor lines become
//                spaces. After this, every remaining token means what it says.
//   2. Tokens -- identifiers and punctuation, each carrying its line.
//   3. Walk   -- a scope stack (namespace / class / block) driven by braces,
//                markers attributed to the class whose body they sit in.
//
// The walk understands exactly as much C++ as attribution needs, and errors
// on everything else it is asked to register. That asymmetry is deliberate:
// code the scanner does not understand is fine as long as it is not *marked*
// -- braces keep the scopes right and the walk strides past it -- but a
// marked declaration it cannot parse is a refusal, never a guess.

namespace RageV
{
	namespace
	{
		bool IsIdentChar(char c)
		{
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
				|| (c >= '0' && c <= '9') || c == '_';
		}

		bool IsIdentStart(char c)
		{
			return IsIdentChar(c) && !(c >= '0' && c <= '9');
		}

		// Comments, string/char literals and preprocessor lines become spaces;
		// newlines survive so line numbers stay true. Raw strings are handled
		// because game code genuinely contains them (any script that writes a
		// file does), and a marker inside one must not register anything.
		std::string Clean(const std::string& text)
		{
			std::string out;
			out.reserve(text.size());

			const size_t n = text.size();
			size_t i = 0;
			bool lineStart = true;   // only whitespace seen since the last newline

			auto blank = [&](char c) { out.push_back(c == '\n' ? '\n' : ' '); };

			while (i < n)
			{
				const char c = text[i];

				// Preprocessor line, continuations included. Markers cannot
				// hide in one -- and neither can a #define that happens to
				// mention them, which would otherwise scan as real.
				if (lineStart && c == '#')
				{
					while (i < n)
					{
						if (text[i] == '\\' && i + 1 < n && text[i + 1] == '\n')
						{
							blank(text[i]); i++;
							blank(text[i]); i++;
							continue;
						}
						if (text[i] == '\n')
							break;
						blank(text[i]); i++;
					}
					continue;
				}

				if (c == '/' && i + 1 < n && text[i + 1] == '/')
				{
					while (i < n && text[i] != '\n') { blank(text[i]); i++; }
					continue;
				}

				if (c == '/' && i + 1 < n && text[i + 1] == '*')
				{
					blank(text[i]); i++;
					blank(text[i]); i++;
					while (i < n && !(text[i] == '*' && i + 1 < n && text[i + 1] == '/'))
					{
						blank(text[i]); i++;
					}
					if (i < n) { blank(text[i]); i++; blank(text[i]); i++; }
					continue;
				}

				// Raw string: R"delim( ... )delim". The R is already in the
				// output; take it back out.
				if (c == '"' && i > 0 && text[i - 1] == 'R'
					&& (i < 2 || !IsIdentChar(text[i - 2])))
				{
					out.back() = ' ';
					blank(text[i]); i++;
					std::string delim;
					while (i < n && text[i] != '(') { delim.push_back(text[i]); blank(text[i]); i++; }
					const std::string close = ")" + delim + "\"";
					while (i < n && text.compare(i, close.size(), close) != 0)
					{
						blank(text[i]); i++;
					}
					for (size_t k = 0; k < close.size() && i < n; k++) { blank(text[i]); i++; }
					continue;
				}

				if (c == '"')
				{
					blank(text[i]); i++;
					while (i < n && text[i] != '"')
					{
						if (text[i] == '\\' && i + 1 < n) { blank(text[i]); i++; }
						blank(text[i]); i++;
					}
					if (i < n) { blank(text[i]); i++; }
					continue;
				}

				// A ' between alphanumerics is a digit separator (1'000), not
				// a character literal -- and swallowing to the "closing" quote
				// there would eat real code.
				if (c == '\'')
				{
					const bool separator =
						i > 0 && IsIdentChar(text[i - 1])
						&& i + 1 < n && IsIdentChar(text[i + 1]);
					if (!separator)
					{
						blank(text[i]); i++;
						while (i < n && text[i] != '\'')
						{
							if (text[i] == '\\' && i + 1 < n) { blank(text[i]); i++; }
							blank(text[i]); i++;
						}
						if (i < n) { blank(text[i]); i++; }
						continue;
					}
				}

				if (c == '\n')
					lineStart = true;
				else if (c != ' ' && c != '\t' && c != '\r')
					lineStart = false;

				out.push_back(c);
				i++;
			}

			return out;
		}

		struct Token
		{
			std::string Text;
			int Line = 0;
			bool Ident = false;
		};

		std::vector<Token> Tokenize(const std::string& clean)
		{
			std::vector<Token> tokens;
			int line = 1;
			size_t i = 0;
			const size_t n = clean.size();

			while (i < n)
			{
				const char c = clean[i];
				if (c == '\n') { line++; i++; continue; }
				if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }

				if (IsIdentStart(c))
				{
					size_t start = i;
					while (i < n && IsIdentChar(clean[i])) i++;
					tokens.push_back({ clean.substr(start, i - start), line, true });
					continue;
				}

				// Numbers are one token nobody reads back; they only need to
				// not be mistaken for identifiers.
				if (c >= '0' && c <= '9')
				{
					size_t start = i;
					while (i < n && (IsIdentChar(clean[i]) || clean[i] == '.')) i++;
					tokens.push_back({ clean.substr(start, i - start), line, false });
					continue;
				}

				// :: as one token -- namespace paths read as ident :: ident.
				if (c == ':' && i + 1 < n && clean[i + 1] == ':')
				{
					tokens.push_back({ "::", line, false });
					i += 2;
					continue;
				}

				tokens.push_back({ std::string(1, c), line, false });
				i++;
			}

			return tokens;
		}

		// The five kinds ScriptFieldKind has, in every spelling a script would
		// use. Validated here so the error names the field and its line; left
		// to the compiler it would name a KindOf overload in a header the
		// script author has never opened.
		bool IsSupportedFieldType(std::string type)
		{
			if (type.rfind("::", 0) == 0)       type = type.substr(2);
			if (type.rfind("RageV::", 0) == 0)  type = type.substr(7);
			return type == "bool" || type == "int" || type == "float"
				|| type == "Vec3" || type == "Math::Vec3"
				|| type == "std::string" || type == "string";
		}

		struct Scope
		{
			enum Kind { Namespace, Class, Block };
			Kind Which = Block;

			// Namespace: the parts one `{` opened -- `namespace A::B {` is one
			// scope with two, because one brace closes both.
			std::vector<std::string> NsParts;

			// Class:
			std::string ClassName;
			int Access = 1;              // 0 public, 1 private, 2 protected
			bool Nested = false;         // declared inside another class
			bool Template = false;
			int ScriptIndex = -1;        // into FileScan::Scripts, once marked
			int Line = 0;
		};

		struct Walker
		{
			const std::vector<Token>& T;
			ScriptGen::FileScan& Scan;
			std::vector<ScriptGen::Error>& Errors;
			const std::filesystem::path& File;

			size_t i = 0;
			std::vector<Scope> Stack;

			bool PendingTemplate = false;
			int  PendingClassMarker = -1;   // line of an RVScript awaiting its class

			void Error(int line, std::string message)
			{
				Errors.push_back({ File, line, std::move(message) });
			}

			bool Have(size_t ahead = 0) const { return i + ahead < T.size(); }
			const Token& Tok(size_t ahead = 0) const { return T[i + ahead]; }

			// ---- the class a marker belongs to -------------------------------

			Scope* CurrentClass()
			{
				if (!Stack.empty() && Stack.back().Which == Scope::Class)
					return &Stack.back();
				return nullptr;
			}

			std::string QualifiedFor(const std::string& className) const
			{
				// Anonymous namespaces contribute nothing to the spelling and
				// do not need to: their members are reachable by qualified
				// lookup in the enclosing namespace, and the wrapper TU
				// includes the file, so internal linkage is no obstacle.
				std::string out;
				for (const Scope& scope : Stack)
					if (scope.Which == Scope::Namespace)
						for (const std::string& part : scope.NsParts)
							if (!part.empty())
								out += "::" + part;
				return out + "::" + className;
			}

			ScriptGen::Script* ScriptFor(Scope& cls, int markerLine)
			{
				if (cls.ScriptIndex >= 0)
					return &Scan.Scripts[cls.ScriptIndex];

				if (cls.Template)
				{
					Error(markerLine, "a template class cannot be a registered script");
					return nullptr;
				}
				if (cls.Nested)
				{
					Error(markerLine, "a nested class cannot be a registered script; "
						"move '" + cls.ClassName + "' to namespace scope");
					return nullptr;
				}

				ScriptGen::Script script;
				script.Name = cls.ClassName;
				script.Line = cls.Line;
				script.Qualified = QualifiedFor(cls.ClassName);

				cls.ScriptIndex = (int)Scan.Scripts.size();
				Scan.Scripts.push_back(std::move(script));
				return &Scan.Scripts[cls.ScriptIndex];
			}

			// ---- constructs --------------------------------------------------

			void SkipAngles()
			{
				// After `template`: the balanced <...>. Nothing inside it can
				// open a class body we care about.
				if (!Have() || Tok().Text != "<")
					return;
				int depth = 0;
				while (Have())
				{
					if (Tok().Text == "<") depth++;
					if (Tok().Text == ">") depth--;
					i++;
					if (depth == 0)
						return;
				}
			}

			void HandleNamespace()
			{
				i++;   // namespace
				Scope scope;
				scope.Which = Scope::Namespace;

				if (Have() && Tok().Text == "{")
				{
					scope.NsParts.push_back("");
					i++;
					Stack.push_back(std::move(scope));
					return;
				}

				while (Have() && Tok().Ident)
				{
					scope.NsParts.push_back(Tok().Text);
					i++;
					if (Have() && Tok().Text == "::")
						i++;
					else
						break;
				}

				if (Have() && Tok().Text == "{")
				{
					i++;
					Stack.push_back(std::move(scope));
					return;
				}

				// namespace alias or using-adjacent; skip the statement.
				while (Have() && Tok().Text != ";") i++;
				if (Have()) i++;
			}

			void HandleClass(bool isStruct)
			{
				const int line = Tok().Line;
				const bool wasEnum = i > 0 && T[i - 1].Ident && T[i - 1].Text == "enum";
				i++;   // class / struct

				// Names and decorations up to the base clause or the body.
				std::string name;
				while (Have() && (Tok().Ident || Tok().Text == "::"))
				{
					if (Tok().Ident && Tok().Text != "final")
						name = Tok().Text;
					i++;
				}

				if (wasEnum)
				{
					// `enum class` is not a class. Its body is a plain block;
					// the general brace handling will balance it.
					return;
				}

				// Forward declaration, or a use of the elaborated type --
				// either way, no body, no scope.
				if (!Have() || (Tok().Text != ":" && Tok().Text != "{"))
				{
					if (PendingClassMarker >= 0)
					{
						Error(PendingClassMarker,
							"RVScript must immediately precede a class definition");
						PendingClassMarker = -1;
					}
					return;
				}

				if (Tok().Text == ":")
				{
					// Base clause: to the `{`, angles balanced so a templated
					// base cannot end it early.
					int angles = 0;
					while (Have())
					{
						if (Tok().Text == "<") angles++;
						if (Tok().Text == ">") angles--;
						if (Tok().Text == "{" && angles == 0) break;
						if (Tok().Text == ";" && angles == 0) return;   // odd, but not a body
						i++;
					}
				}

				if (!Have() || Tok().Text != "{")
					return;
				i++;   // {

				Scope scope;
				scope.Which = Scope::Class;
				scope.ClassName = name;
				scope.Access = isStruct ? 0 : 1;
				scope.Line = line;
				scope.Template = PendingTemplate;
				for (const Scope& outer : Stack)
					if (outer.Which == Scope::Class)
						scope.Nested = true;
				PendingTemplate = false;

				Stack.push_back(std::move(scope));

				if (PendingClassMarker >= 0)
				{
					ScriptFor(Stack.back(), PendingClassMarker);
					PendingClassMarker = -1;
				}
			}

			void HandleAccess()
			{
				const std::string& word = Tok().Text;
				if (Have(1) && Tok(1).Text == ":" && CurrentClass())
				{
					Scope* cls = CurrentClass();
					cls->Access = word == "public" ? 0 : word == "protected" ? 2 : 1;
					i += 2;
					return;
				}
				i++;
			}

			void HandleLegacy()
			{
				const int line = Tok().Line;
				i++;   // RV_REGISTER_SCRIPT
				if (Have() && Tok().Text == "(" && Have(1) && Tok(1).Ident)
				{
					Scan.Legacy.push_back({ Tok(1).Text, line });
					i += 2;
				}
			}

			// A marked member must be public: the emitted code names it from
			// outside the class, exactly as a hand-written registration would
			// have to. The message says so rather than leaving the compile of
			// a file nobody wrote to say it worse.
			bool RequirePublic(Scope& cls, int line, const char* what)
			{
				if (cls.Access == 0)
					return true;
				Error(line, std::string(what) + " must be public: the generated "
					"registration names it from outside the class");
				return false;
			}

			void HandleField()
			{
				const int markerLine = Tok().Line;
				i++;   // RVShowInEditor

				Scope* cls = CurrentClass();
				if (!cls)
				{
					Error(markerLine, "RVShowInEditor must sit directly inside a "
						"script class, on the member it marks");
					return;
				}

				// The declaration: tokens up to what ends or initialises it.
				std::vector<Token> decl;
				while (Have() && Tok().Text != ";" && Tok().Text != "="
					&& Tok().Text != "{" && Tok().Text != "(")
				{
					if (Tok().Text == "[")
					{
						Error(markerLine, "cannot parse this declaration; arrays and "
							"attributes are not supported on marked members");
						return;
					}
					decl.push_back(Tok());
					i++;
				}

				if (Have() && Tok().Text == "(")
				{
					Error(markerLine, "RVShowInEditor marks a data member; "
						"use RVCallable for a method");
					return;
				}

				for (const Token& token : decl)
				{
					const std::string& word = token.Text;
					if (word == "static" || word == "constexpr" || word == "const"
						|| word == "inline" || word == "mutable" || word == "thread_local")
					{
						Error(markerLine, "a marked field cannot be " + word
							+ ": the inspector writes it on a live instance");
						return;
					}
				}

				// Name is the last identifier; everything before it is the type.
				std::string name;
				size_t nameAt = decl.size();
				for (size_t k = decl.size(); k-- > 0;)
				{
					if (decl[k].Ident) { name = decl[k].Text; nameAt = k; break; }
				}
				if (name.empty())
				{
					Error(markerLine, "RVShowInEditor is not followed by a member declaration");
					return;
				}

				std::string type;
				for (size_t k = 0; k < nameAt; k++)
				{
					if (!type.empty() && decl[k].Text != "::" && type.rfind("::") != type.size() - 2)
						type += " ";
					type += decl[k].Text;
				}

				if (!IsSupportedFieldType(type))
				{
					Error(markerLine, "'" + type + "' is not an editable field type; "
						"a script field is a bool, an int, a float, a Vec3 or a "
						"std::string -- the same five kinds C# fields have");
					return;
				}

				if (!RequirePublic(*cls, markerLine, "a marked field"))
					return;

				// Initialiser, if any: balanced to the `;`. A comma at depth
				// zero is a second declarator, which would register only the
				// first name and look like the tool losing one.
				if (Have() && (Tok().Text == "=" || Tok().Text == "{"))
				{
					int round = 0, curly = 0, square = 0;
					while (Have())
					{
						const std::string& t = Tok().Text;
						if (t == "(") round++;
						if (t == ")") round--;
						if (t == "{") curly++;
						if (t == "}") curly--;
						if (t == "[") square++;
						if (t == "]") square--;
						if (t == "," && round == 0 && curly == 0 && square == 0)
						{
							Error(markerLine, "one member per RVShowInEditor; "
								"declare '" + name + "' and its neighbour separately");
							return;
						}
						if (t == ";" && round == 0 && curly == 0 && square == 0)
							break;
						i++;
					}
				}
				if (Have() && Tok().Text == ";")
					i++;

				ScriptGen::Script* script = ScriptFor(*cls, markerLine);
				if (!script)
					return;

				for (const ScriptGen::Field& field : script->Fields)
					if (field.Name == name)
					{
						Error(markerLine, "field '" + name + "' is marked twice");
						return;
					}

				script->Fields.push_back({ name, type, markerLine });
			}

			void HandleMethod()
			{
				const int markerLine = Tok().Line;
				i++;   // RVCallable

				Scope* cls = CurrentClass();
				if (!cls)
				{
					Error(markerLine, "RVCallable must sit directly inside a "
						"script class, on the method it marks");
					return;
				}

				std::vector<Token> decl;
				while (Have() && Tok().Text != "(" && Tok().Text != ";" && Tok().Text != "{")
				{
					decl.push_back(Tok());
					i++;
				}

				if (!Have() || Tok().Text != "(")
				{
					Error(markerLine, "RVCallable marks a method; "
						"use RVShowInEditor for a data member");
					return;
				}

				std::string name;
				size_t nameAt = decl.size();
				for (size_t k = decl.size(); k-- > 0;)
				{
					if (decl[k].Ident) { name = decl[k].Text; nameAt = k; break; }
				}
				if (name.empty())
				{
					Error(markerLine, "RVCallable is not followed by a method declaration");
					return;
				}

				// Return type and qualifiers. `virtual` and `inline` change
				// nothing about how the registry calls it; `static` does --
				// a handler runs on the entity that owns it.
				std::string ret;
				for (size_t k = 0; k < nameAt; k++)
				{
					const std::string& word = decl[k].Text;
					if (word == "virtual" || word == "inline")
						continue;
					if (word == "static")
					{
						Error(markerLine, "a callable method cannot be static: "
							"it is invoked on the entity's own script instance");
						return;
					}
					if (!ret.empty())
						ret += " ";
					ret += word;
				}
				if (ret != "void")
				{
					Error(markerLine, "a callable method is void with no parameters: "
						"a scene file has nowhere to store arguments and nowhere "
						"to put a return value");
					return;
				}

				i++;   // (
				if (Have() && Tok().Ident && Tok().Text == "void")
					i++;
				if (!Have() || Tok().Text != ")")
				{
					Error(markerLine, "a callable method is void with no parameters: "
						"a scene file has nowhere to store arguments and nowhere "
						"to put a return value");
					return;
				}
				i++;   // )

				// Trailing specifiers, up to the body or the semicolon. The
				// body's `{` is left for the walker, which balances it.
				while (Have() && Tok().Text != ";" && Tok().Text != "{")
				{
					if (Tok().Text == "const")
					{
						Error(markerLine, "a callable method cannot be const: "
							"handlers are allowed to change the entity they run on");
						return;
					}
					i++;
				}
				if (Have() && Tok().Text == ";")
					i++;

				if (!RequirePublic(*cls, markerLine, "a marked method"))
					return;

				ScriptGen::Script* script = ScriptFor(*cls, markerLine);
				if (!script)
					return;

				for (const ScriptGen::Method& method : script->Methods)
					if (method.Name == name)
					{
						Error(markerLine, "method '" + name + "' is marked twice");
						return;
					}

				script->Methods.push_back({ name, markerLine });
			}

			// ---- the walk ----------------------------------------------------

			void Run()
			{
				while (Have())
				{
					const Token& token = Tok();

					if (token.Ident)
					{
						if (token.Text == "namespace") { HandleNamespace(); continue; }
						if (token.Text == "template")  { i++; SkipAngles(); PendingTemplate = true; continue; }
						if (token.Text == "class")     { HandleClass(false); continue; }
						if (token.Text == "struct")    { HandleClass(true); continue; }
						if (token.Text == "public" || token.Text == "private"
							|| token.Text == "protected") { HandleAccess(); continue; }
						if (token.Text == "RV_REGISTER_SCRIPT") { HandleLegacy(); continue; }
						if (token.Text == "RVShowInEditor") { HandleField(); continue; }
						if (token.Text == "RVCallable")     { HandleMethod(); continue; }
						if (token.Text == "RVScript")
						{
							if (PendingClassMarker >= 0)
								Error(PendingClassMarker,
									"RVScript must immediately precede a class definition");
							PendingClassMarker = token.Line;
							i++;
							continue;
						}
						i++;
						continue;
					}

					if (token.Text == "{")
					{
						if (PendingClassMarker >= 0)
						{
							Error(PendingClassMarker,
								"RVScript must immediately precede a class definition");
							PendingClassMarker = -1;
						}
						Scope scope;
						scope.Which = Scope::Block;
						Stack.push_back(scope);
						i++;
						continue;
					}

					if (token.Text == "}")
					{
						if (!Stack.empty())
							Stack.pop_back();
						i++;
						continue;
					}

					if (token.Text == ";" && PendingClassMarker >= 0)
					{
						Error(PendingClassMarker,
							"RVScript must immediately precede a class definition");
						PendingClassMarker = -1;
					}

					// A template that led to a function or a variable rather
					// than a class: the flag has nothing left to apply to.
					if (token.Text == ";" || token.Text == "(")
						PendingTemplate = false;

					i++;
				}

				if (PendingClassMarker >= 0)
					Error(PendingClassMarker,
						"RVScript must immediately precede a class definition");
			}
		};
	}

	std::string ScriptGen::Error::Format() const
	{
		// The MSVC diagnostic shape, verbatim -- ModuleBuild::ParseDiagnostic
		// already recognises it, so a generator refusal lands in the editor's
		// build panel like any other compile error, file link and all.
		std::ostringstream out;
		out << File.string() << "(" << Line << "): error RVGEN1: " << Message;
		return out.str();
	}

	ScriptGen::FileScan ScriptGen::Scan(const std::filesystem::path& file,
										const std::string& text,
										std::vector<Error>& errors)
	{
		FileScan scan;
		scan.Path = file;

		const std::string clean = Clean(text);
		const std::vector<Token> tokens = Tokenize(clean);

		Walker walker{ tokens, scan, errors, file };
		walker.Run();

		// Marked and legacy-registered is a class registered twice: the
		// registry would warn at load and first-wins, which is a coin toss
		// over link order. Refusing here names both lines instead.
		for (const Script& script : scan.Scripts)
			for (const LegacyRegistration& legacy : scan.Legacy)
				if (legacy.Name == script.Name)
					errors.push_back({ file, script.Line,
						"'" + script.Name + "' uses declaration-site markers and "
						"RV_REGISTER_SCRIPT (line " + std::to_string(legacy.Line)
						+ "); remove one -- the markers replace the block" });

		// Two marked classes under one name -- one file, two namespaces. The
		// registered name is what scene files store, so it has to be unique;
		// rvgen repeats this check across files for the same reason.
		for (size_t a = 0; a < scan.Scripts.size(); a++)
			for (size_t b = a + 1; b < scan.Scripts.size(); b++)
				if (scan.Scripts[a].Name == scan.Scripts[b].Name)
					errors.push_back({ file, scan.Scripts[b].Line,
						"two script classes are both named '" + scan.Scripts[a].Name
						+ "' (the other is at line "
						+ std::to_string(scan.Scripts[a].Line) + "); registered "
						"names are what scene files store, so they must be unique" });

		return scan;
	}

	std::string ScriptGen::Emit(const FileScan& scan)
	{
		if (scan.Scripts.empty())
			return {};

		std::ostringstream out;
		const std::string from = scan.Path.filename().string();

		out << "// Generated by rvgen from " << from << " -- do not edit; every\n";
		out << "// configure overwrites it. This wrapper compiles the source file and\n";
		out << "// appends the registrations its markers describe: a script class\n";
		out << "// defined in a .cpp can only be named by a translation unit that\n";
		out << "// includes it, so the file compiles through here instead of directly.\n";
		out << "#include \"" << scan.Path.generic_string() << "\"\n";

		for (const Script& script : scan.Scripts)
		{
			out << "\n";
			out << "static ::RageV::ScriptRegistry::Registration s_RvGen" << script.Name << " =\n";
			out << "\t::RageV::Detail::ScriptRegistrar::Make<" << script.Qualified
				<< ">(\"" << script.Name << "\")";

			for (const Field& field : script.Fields)
				out << "\n\t\t.Field<&" << script.Qualified << "::" << field.Name
					<< ">(\"" << field.Name << "\")";
			for (const Method& method : script.Methods)
				out << "\n\t\t.Method<&" << script.Qualified << "::" << method.Name
					<< ">(\"" << method.Name << "\")";

			out << ";\n";
		}

		return out.str();
	}

	bool ScriptGen::HasMarkers(const std::string& text)
	{
		const std::string clean = Clean(text);
		for (const char* marker : { "RVShowInEditor", "RVCallable", "RVScript" })
		{
			const size_t length = std::string_view(marker).size();
			size_t at = clean.find(marker);
			while (at != std::string::npos)
			{
				const bool before = at == 0 || !IsIdentChar(clean[at - 1]);
				const bool after = at + length >= clean.size() || !IsIdentChar(clean[at + length]);
				if (before && after)
					return true;
				at = clean.find(marker, at + 1);
			}
		}
		return false;
	}
}
