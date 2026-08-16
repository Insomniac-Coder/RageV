// rvdoc -- the developer manual generator.
//
// Turns the Markdown under docs/manual into a static HTML site that a person
// writing a game in this engine can actually use: searchable, deep-linkable,
// and readable from a local folder without a server.
//
// Why generated HTML rather than a hand-written page or a PDF. Documentation
// does not fail by looking bad, it fails by drifting -- the API changes, the
// prose does not, and the manual quietly starts lying. Markdown in the
// repository means a doc change lands in the same diff as the code change that
// forced it, where a reviewer sees both. A generator means the manual can be
// *checked* against the headers rather than trusted, which is the only
// mechanism that has ever kept documentation honest. A PDF can do neither: it
// is a binary blob nobody can review and everybody forgets to rebuild.
//
// Deliberately no new submodule. This engine vendors fourteen already, and a
// documentation tool is not worth a fifteenth -- the Markdown accepted here is
// a subset chosen to cover what technical writing needs and nothing else.
//
//     rvdoc [--in docs/manual] [--out build/docs] [--check]
//
// --check reports drift and returns non-zero without writing anything, which
// is the form the build uses.
#include <rvpch.h>
#include "RageV/Core/Log.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
	// --- small string helpers -----------------------------------------------

	std::string Trim(std::string value)
	{
		const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
		value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
		value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
		return value;
	}

	std::string ToLower(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(),
					   [](unsigned char c) { return (char)std::tolower(c); });
		return value;
	}

	bool StartsWith(const std::string& value, const std::string& prefix)
	{
		return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
	}

	// Leading spaces, with a tab counting as four. Indentation decides list
	// nesting, so a file mixing tabs and spaces has to nest the way it looks.
	size_t IndentOf(const std::string& line)
	{
		size_t indent = 0;
		for (char c : line)
		{
			if (c == ' ')       indent += 1;
			else if (c == '\t') indent += 4;
			else                break;
		}
		return indent;
	}

	std::string Escape(const std::string& value)
	{
		std::string out;
		out.reserve(value.size());
		for (char c : value)
		{
			switch (c)
			{
				case '&':  out += "&amp;";  break;
				case '<':  out += "&lt;";   break;
				case '>':  out += "&gt;";   break;
				case '"':  out += "&quot;"; break;
				case '\'': out += "&#39;";  break;
				default:   out += c;        break;
			}
		}
		return out;
	}

	// For a JavaScript string literal in the search index.
	std::string EscapeJs(const std::string& value)
	{
		std::string out;
		out.reserve(value.size() + 8);
		for (unsigned char c : value)
		{
			switch (c)
			{
				case '\\': out += "\\\\"; break;
				case '"':  out += "\\\""; break;
				case '\n': out += "\\n";  break;
				case '\r': break;
				case '\t': out += " ";    break;
				// Closing a script element from inside a string literal ends the
				// element, whatever the quoting says.
				case '<':  out += "\\u003c"; break;
				default:
					if (c < 0x20) out += ' ';
					else          out += (char)c;
					break;
			}
		}
		return out;
	}

	// "Forces and impulses" -> "forces-and-impulses". Anchors are API the moment
	// anyone links to one, so this has to stay stable.
	std::string Slugify(const std::string& text)
	{
		std::string out;
		bool pendingDash = false;
		for (unsigned char c : text)
		{
			if (std::isalnum(c))
			{
				if (pendingDash && !out.empty())
					out += '-';
				pendingDash = false;
				out += (char)std::tolower(c);
			}
			else
			{
				pendingDash = true;
			}
		}
		return out.empty() ? "section" : out;
	}

	std::string ReadFile(const fs::path& path, bool& ok)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
		{
			ok = false;
			return {};
		}
		ok = true;
		std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		// Normalise line endings once, here, so nothing downstream has to care.
		text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
		return text;
	}

	std::vector<std::string> SplitLines(const std::string& text)
	{
		std::vector<std::string> lines;
		std::istringstream stream(text);
		std::string line;
		while (std::getline(stream, line))
			lines.push_back(line);
		return lines;
	}

	// --- syntax highlighting -------------------------------------------------
	//
	// A tokeniser, not a parser. It knows comments, strings, numbers, keywords
	// and preprocessor lines, which is every distinction that helps someone
	// reading a twenty-line example. Anything more would be a compiler.

	struct Language
	{
		const char* LineComment = nullptr;
		const char* BlockOpen = nullptr;
		const char* BlockClose = nullptr;
		bool        Preprocessor = false;
		std::set<std::string> Keywords;
		std::set<std::string> Types;
	};

	const Language& LanguageFor(const std::string& name)
	{
		static const std::set<std::string> cppKeywords = {
			"alignas","auto","bool","break","case","catch","class","const","constexpr","continue",
			"default","delete","do","else","enum","explicit","export","extern","false","for",
			"friend","if","inline","mutable","namespace","new","noexcept","nullptr","operator",
			"override","private","protected","public","return","sizeof","static","struct","switch",
			"template","this","throw","true","try","typedef","typename","union","using","virtual",
			"void","while"
		};
		static const std::set<std::string> cppTypes = {
			"char","double","float","int","long","short","signed","unsigned","size_t",
			"uint8_t","uint16_t","uint32_t","uint64_t","int32_t","int64_t",
			"std","glm","vec2","vec3","vec4","mat4","string","vector",
			"Entity","Scene","Timestep","UUID","AssetHandle","Collision","RayHit","AudioVoice",
			"ScriptableEntity","TransformComponent","MeshComponent","RigidBodyComponent"
		};
		static const std::set<std::string> csKeywords = {
			"abstract","as","base","bool","break","case","catch","class","const","continue",
			"default","delegate","do","else","enum","event","explicit","extern","false","finally",
			"fixed","for","foreach","get","if","in","interface","internal","is","lock","namespace",
			"new","null","operator","out","override","params","partial","private","protected",
			"public","readonly","ref","return","sealed","set","static","struct","switch","this",
			"throw","true","try","typeof","unsafe","using","var","virtual","void","while"
		};
		static const std::set<std::string> csTypes = {
			"byte","char","decimal","double","float","int","long","object","sbyte","short",
			"string","uint","ulong","ushort","IntPtr","Vector3","Quaternion","Entity","Script",
			"Collision","RayHit","Time","Input","Physics","Audio","Transform"
		};
		static const std::set<std::string> shellKeywords = {
			"cd","cmake","echo","export","for","git","if","in","then","fi","do","done","set","dotnet"
		};

		static const Language cpp{ "//", "/*", "*/", true, cppKeywords, cppTypes };
		static const Language csharp{ "//", "/*", "*/", true, csKeywords, csTypes };
		static const Language shell{ "#", nullptr, nullptr, false, shellKeywords, {} };
		static const Language ini{ ";", nullptr, nullptr, false, {}, {} };
		static const Language plain{};

		const std::string lowered = ToLower(name);
		if (lowered == "cpp" || lowered == "c++" || lowered == "c" || lowered == "glsl" || lowered == "hlsl")
			return cpp;
		if (lowered == "csharp" || lowered == "cs" || lowered == "c#")
			return csharp;
		if (lowered == "bash" || lowered == "sh" || lowered == "shell" || lowered == "console")
			return shell;
		if (lowered == "ini" || lowered == "yaml" || lowered == "yml" || lowered == "toml")
			return ini;
		return plain;
	}

	std::string Highlight(const std::string& code, const std::string& languageName)
	{
		const Language& language = LanguageFor(languageName);
		std::string out;
		out.reserve(code.size() * 2);

		const size_t size = code.size();
		size_t i = 0;
		bool atLineStart = true;

		const auto span = [&out](const char* cls, const std::string& text)
		{
			out += "<span class=\"tok-";
			out += cls;
			out += "\">";
			out += Escape(text);
			out += "</span>";
		};

		while (i < size)
		{
			const char c = code[i];

			if (c == '\n')
			{
				out += '\n';
				atLineStart = true;
				i++;
				continue;
			}

			if (std::isspace((unsigned char)c))
			{
				out += c;
				i++;
				continue;
			}

			// Preprocessor: the whole line, including any comment on it. Close
			// enough, and it keeps #include <foo> from being read as a string.
			if (atLineStart && language.Preprocessor && c == '#')
			{
				const size_t end = code.find('\n', i);
				span("pre", code.substr(i, end == std::string::npos ? std::string::npos : end - i));
				i = (end == std::string::npos) ? size : end;
				continue;
			}
			atLineStart = false;

			if (language.LineComment && code.compare(i, strlen(language.LineComment), language.LineComment) == 0)
			{
				const size_t end = code.find('\n', i);
				span("comment", code.substr(i, end == std::string::npos ? std::string::npos : end - i));
				i = (end == std::string::npos) ? size : end;
				continue;
			}

			if (language.BlockOpen && code.compare(i, strlen(language.BlockOpen), language.BlockOpen) == 0)
			{
				const size_t end = code.find(language.BlockClose, i);
				const size_t stop = (end == std::string::npos) ? size : end + strlen(language.BlockClose);
				span("comment", code.substr(i, stop - i));
				i = stop;
				continue;
			}

			if (c == '"' || c == '\'')
			{
				size_t j = i + 1;
				while (j < size && code[j] != c && code[j] != '\n')
					j += (code[j] == '\\') ? 2 : 1;
				if (j < size && code[j] == c)
					j++;
				span("string", code.substr(i, j - i));
				i = j;
				continue;
			}

			if (std::isdigit((unsigned char)c))
			{
				size_t j = i;
				while (j < size && (std::isalnum((unsigned char)code[j]) || code[j] == '.'))
					j++;
				span("number", code.substr(i, j - i));
				i = j;
				continue;
			}

			if (std::isalpha((unsigned char)c) || c == '_')
			{
				size_t j = i;
				while (j < size && (std::isalnum((unsigned char)code[j]) || code[j] == '_'))
					j++;
				const std::string word = code.substr(i, j - i);

				// A name followed by '(' reads as a call whatever it is, and
				// highlighting it as one is what a reader expects.
				size_t after = j;
				while (after < size && (code[after] == ' ' || code[after] == '\t'))
					after++;

				if (language.Keywords.count(word))
					span("keyword", word);
				else if (language.Types.count(word))
					span("type", word);
				else if (after < size && code[after] == '(')
					span("call", word);
				else
					out += Escape(word);

				i = j;
				continue;
			}

			out += Escape(std::string(1, c));
			i++;
		}

		return out;
	}

	// --- inline Markdown -----------------------------------------------------

	std::string RenderInline(const std::string& text);

	// `code`, **strong**, *emphasis*, [text](target), and escaping for
	// everything else. Code spans are consumed first, so `**not bold**` inside
	// backticks stays literal -- which matters in a manual full of operators.
	std::string RenderInline(const std::string& text)
	{
		std::string out;
		const size_t size = text.size();

		for (size_t i = 0; i < size; )
		{
			const char c = text[i];

			if (c == '\\' && i + 1 < size)
			{
				out += Escape(std::string(1, text[i + 1]));
				i += 2;
				continue;
			}

			if (c == '`')
			{
				const size_t end = text.find('`', i + 1);
				if (end != std::string::npos)
				{
					out += "<code>" + Escape(text.substr(i + 1, end - i - 1)) + "</code>";
					i = end + 1;
					continue;
				}
			}

			if (c == '*' && i + 1 < size && text[i + 1] == '*')
			{
				const size_t end = text.find("**", i + 2);
				if (end != std::string::npos)
				{
					out += "<strong>" + RenderInline(text.substr(i + 2, end - i - 2)) + "</strong>";
					i = end + 2;
					continue;
				}
			}

			if (c == '*' || c == '_')
			{
				const size_t end = text.find(c, i + 1);
				if (end != std::string::npos && end > i + 1)
				{
					out += "<em>" + RenderInline(text.substr(i + 1, end - i - 1)) + "</em>";
					i = end + 1;
					continue;
				}
			}

			if (c == '[')
			{
				const size_t close = text.find(']', i);
				if (close != std::string::npos && close + 1 < size && text[close + 1] == '(')
				{
					const size_t target = text.find(')', close);
					if (target != std::string::npos)
					{
						const std::string label = text.substr(i + 1, close - i - 1);
						std::string href = Trim(text.substr(close + 2, target - close - 2));

						// A link between manual pages is written as it is on
						// disk, so the Markdown stays readable in an editor and
						// on GitHub. Rewriting the extension here is what makes
						// both work.
						const size_t hash = href.find('#');
						std::string anchor = (hash == std::string::npos) ? "" : href.substr(hash);
						std::string path = (hash == std::string::npos) ? href : href.substr(0, hash);
						if (path.size() > 3 && path.compare(path.size() - 3, 3, ".md") == 0)
							path = path.substr(0, path.size() - 3) + ".html";

						const bool external = StartsWith(path, "http://") || StartsWith(path, "https://");
						out += "<a href=\"" + Escape(path + anchor) + "\"";
						if (external)
							out += " target=\"_blank\" rel=\"noopener\"";
						out += ">" + RenderInline(label) + "</a>";
						i = target + 1;
						continue;
					}
				}
			}

			out += Escape(std::string(1, c));
			i++;
		}

		return out;
	}

	// Inline markup stripped, for the search index and for <title>.
	std::string PlainText(const std::string& text)
	{
		std::string out;
		for (size_t i = 0; i < text.size(); i++)
		{
			const char c = text[i];
			if (c == '`' || c == '*' || c == '_' || c == '#')
				continue;
			if (c == '[')
			{
				const size_t close = text.find(']', i);
				const size_t target = (close == std::string::npos) ? std::string::npos : text.find(')', close);
				if (close != std::string::npos && target != std::string::npos)
				{
					out += PlainText(text.substr(i + 1, close - i - 1));
					i = target;
					continue;
				}
			}
			out += c;
		}
		return Trim(out);
	}

	// --- block Markdown ------------------------------------------------------

	struct Heading
	{
		int         Level = 2;
		std::string Text;
		std::string Anchor;
	};

	struct Rendered
	{
		std::string Html;
		std::string Title;
		std::vector<Heading> Headings;
		std::string SearchText;
	};

	bool IsTableSeparator(const std::string& line)
	{
		const std::string trimmed = Trim(line);
		if (trimmed.find('|') == std::string::npos)
			return false;
		for (char c : trimmed)
		{
			if (c != '|' && c != '-' && c != ':' && c != ' ')
				return false;
		}
		return trimmed.find('-') != std::string::npos;
	}

	std::vector<std::string> SplitRow(const std::string& line)
	{
		std::string trimmed = Trim(line);
		if (!trimmed.empty() && trimmed.front() == '|') trimmed.erase(trimmed.begin());
		if (!trimmed.empty() && trimmed.back() == '|')  trimmed.pop_back();

		std::vector<std::string> cells;
		std::string cell;
		for (size_t i = 0; i < trimmed.size(); i++)
		{
			if (trimmed[i] == '\\' && i + 1 < trimmed.size())
			{
				cell += trimmed[i];
				cell += trimmed[i + 1];
				i++;
				continue;
			}
			if (trimmed[i] == '|')
			{
				cells.push_back(Trim(cell));
				cell.clear();
				continue;
			}
			cell += trimmed[i];
		}
		cells.push_back(Trim(cell));
		return cells;
	}

	Rendered RenderMarkdown(const std::vector<std::string>& lines)
	{
		Rendered result;
		std::string& out = result.Html;

		// Open list levels, so nesting closes in the right order and a nested
		// list ends up *inside* its parent item rather than beside it -- which
		// is the difference between indented sub-points and a flat second list.
		struct ListLevel
		{
			size_t Indent = 0;
			bool   Ordered = false;
			bool   ItemOpen = false;
		};
		std::vector<ListLevel> listStack;
		std::string paragraph;

		const auto closeParagraph = [&]()
		{
			if (paragraph.empty())
				return;
			out += "<p>" + RenderInline(Trim(paragraph)) + "</p>\n";
			result.SearchText += PlainText(paragraph) + " ";
			paragraph.clear();
		};

		const auto closeLists = [&](size_t downTo)
		{
			while (!listStack.empty() && listStack.back().Indent >= downTo)
			{
				if (listStack.back().ItemOpen)
					out += "</li>\n";
				out += listStack.back().Ordered ? "</ol>\n" : "</ul>\n";
				listStack.pop_back();
			}
		};

		for (size_t index = 0; index < lines.size(); index++)
		{
			const std::string& raw = lines[index];
			const std::string line = Trim(raw);

			// --- fenced code ---
			if (StartsWith(line, "```"))
			{
				closeParagraph();
				closeLists(0);

				const std::string language = Trim(line.substr(3));
				std::string code;
				index++;
				while (index < lines.size() && !StartsWith(Trim(lines[index]), "```"))
				{
					code += lines[index];
					code += '\n';
					index++;
				}

				out += "<figure class=\"code\">";
				if (!language.empty())
					out += "<figcaption>" + Escape(language) + "</figcaption>";
				out += "<pre><code>" + Highlight(code, language) + "</code></pre></figure>\n";
				continue;
			}

			if (line.empty())
			{
				closeParagraph();
				continue;
			}

			// --- heading ---
			if (line[0] == '#')
			{
				closeParagraph();
				closeLists(0);

				int level = 0;
				while (level < (int)line.size() && line[level] == '#')
					level++;

				const std::string text = Trim(line.substr(level));
				const std::string anchor = Slugify(PlainText(text));

				if (level == 1 && result.Title.empty())
					result.Title = PlainText(text);
				else
					result.Headings.push_back({ level, PlainText(text), anchor });

				const std::string tag = "h" + std::to_string(RageV::Math::Min(level, 6));
				out += "<" + tag + " id=\"" + Escape(anchor) + "\">" + RenderInline(text);
				// A heading is the thing people link to, so every one carries a
				// link to itself rather than only the ones somebody remembered.
				out += "<a class=\"anchor\" href=\"#" + Escape(anchor) + "\" aria-label=\"Link to this section\">#</a>";
				out += "</" + tag + ">\n";
				result.SearchText += PlainText(text) + " ";
				continue;
			}

			// --- horizontal rule ---
			if (line == "---" || line == "***" || line == "___")
			{
				closeParagraph();
				closeLists(0);
				out += "<hr>\n";
				continue;
			}

			// --- callout or quote ---
			if (line[0] == '>')
			{
				closeParagraph();
				closeLists(0);

				std::vector<std::string> quoted;
				while (index < lines.size() && !Trim(lines[index]).empty() && Trim(lines[index])[0] == '>')
				{
					std::string inner = Trim(lines[index]).substr(1);
					if (!inner.empty() && inner[0] == ' ')
						inner.erase(inner.begin());
					quoted.push_back(inner);
					index++;
				}
				index--;

				// GitHub's alert syntax, plus one of this project's own. A trap
				// is not a warning: it is something that fails *silently*, which
				// is the category this engine keeps being bitten by and the one
				// worth a distinct colour.
				std::string kind, label;
				if (!quoted.empty())
				{
					const std::string first = Trim(quoted.front());
					if (StartsWith(first, "[!NOTE]"))         { kind = "note";    label = "Note"; }
					else if (StartsWith(first, "[!TIP]"))     { kind = "tip";     label = "Tip"; }
					else if (StartsWith(first, "[!WARNING]")) { kind = "warning"; label = "Warning"; }
					else if (StartsWith(first, "[!TRAP]"))    { kind = "trap";    label = "Trap"; }
					if (!kind.empty())
					{
						quoted.front() = Trim(first.substr(first.find(']') + 1));
						if (quoted.front().empty())
							quoted.erase(quoted.begin());
					}
				}

				const Rendered inner = RenderMarkdown(quoted);
				if (kind.empty())
				{
					out += "<blockquote>" + inner.Html + "</blockquote>\n";
				}
				else
				{
					out += "<aside class=\"callout callout-" + kind + "\">";
					out += "<p class=\"callout-label\">" + label + "</p>";
					out += inner.Html + "</aside>\n";
				}
				result.SearchText += inner.SearchText;
				continue;
			}

			// --- table ---
			if (line.find('|') != std::string::npos && index + 1 < lines.size() &&
				IsTableSeparator(lines[index + 1]))
			{
				closeParagraph();
				closeLists(0);

				const std::vector<std::string> header = SplitRow(line);
				const std::vector<std::string> rule = SplitRow(lines[index + 1]);

				std::vector<const char*> align(header.size(), "left");
				for (size_t c = 0; c < rule.size() && c < align.size(); c++)
				{
					const bool left = !rule[c].empty() && rule[c].front() == ':';
					const bool right = !rule[c].empty() && rule[c].back() == ':';
					align[c] = right ? (left ? "center" : "right") : "left";
				}

				out += "<div class=\"table-scroll\"><table><thead><tr>";
				for (size_t c = 0; c < header.size(); c++)
					out += "<th style=\"text-align:" + std::string(align[c]) + "\">" + RenderInline(header[c]) + "</th>";
				out += "</tr></thead><tbody>\n";

				index += 2;
				while (index < lines.size() && Trim(lines[index]).find('|') != std::string::npos &&
					   !Trim(lines[index]).empty())
				{
					const std::vector<std::string> cells = SplitRow(lines[index]);
					out += "<tr>";
					for (size_t c = 0; c < cells.size(); c++)
					{
						const char* a = c < align.size() ? align[c] : "left";
						out += "<td style=\"text-align:" + std::string(a) + "\">" + RenderInline(cells[c]) + "</td>";
						result.SearchText += PlainText(cells[c]) + " ";
					}
					out += "</tr>\n";
					index++;
				}
				index--;

				out += "</tbody></table></div>\n";
				continue;
			}

			// --- list ---
			const bool bullet = (StartsWith(line, "- ") || StartsWith(line, "* "));
			bool ordered = false;
			size_t markerLength = 0;
			if (!bullet)
			{
				size_t digits = 0;
				while (digits < line.size() && std::isdigit((unsigned char)line[digits]))
					digits++;
				if (digits > 0 && digits + 1 < line.size() && line[digits] == '.' && line[digits + 1] == ' ')
				{
					ordered = true;
					markerLength = digits + 2;
				}
			}
			else
			{
				markerLength = 2;
			}

			if (bullet || ordered)
			{
				closeParagraph();

				const size_t indent = IndentOf(raw);
				closeLists(indent + 1);

				if (listStack.empty() || listStack.back().Indent < indent)
				{
					// Opened while the parent's item is still open, so the
					// nested list nests.
					out += ordered ? "<ol>\n" : "<ul>\n";
					listStack.push_back({ indent, ordered, false });
				}

				if (listStack.back().ItemOpen)
					out += "</li>\n";

				const std::string item = Trim(line.substr(markerLength));
				out += "<li>" + RenderInline(item);
				listStack.back().ItemOpen = true;
				result.SearchText += PlainText(item) + " ";
				continue;
			}

			// --- a wrapped list item ---
			//
			// Markdown calls this lazy continuation: a plain line directly under
			// a list item belongs to that item. Without it a bullet that happens
			// to wrap in the source renders as one bullet followed by a stray
			// paragraph, which is how nearly every hand-rolled parser gets this
			// wrong and is very visible on a page of prose.
			const bool afterBlank = (index == 0) || Trim(lines[index - 1]).empty();
			if (!listStack.empty() && listStack.back().ItemOpen && !afterBlank)
			{
				out += " " + RenderInline(line);
				result.SearchText += PlainText(line) + " ";
				continue;
			}

			closeLists(0);

			// --- paragraph ---
			if (!paragraph.empty())
				paragraph += ' ';
			paragraph += line;
		}

		closeParagraph();
		closeLists(0);
		return result;
	}

	// --- the site ------------------------------------------------------------

	struct Page
	{
		fs::path    Source;
		std::string Output;      // "scripting/cpp.html"
		std::string NavTitle;
		int         NavDepth = 0;
		Rendered    Content;
	};

	// docs/manual/SUMMARY.md is the table of contents, written as a nested list
	// of links -- one place that decides both the sidebar order and what "next"
	// means, and a file that stays readable when GitHub renders it.
	std::vector<Page> ParseSummary(const fs::path& root, bool& ok)
	{
		std::vector<Page> pages;

		bool read = false;
		const std::string text = ReadFile(root / "SUMMARY.md", read);
		if (!read)
		{
			RV_CORE_ERROR("No SUMMARY.md in {0} -- that file is the manual's table of contents", root.string());
			ok = false;
			return pages;
		}

		for (const std::string& raw : SplitLines(text))
		{
			const std::string line = Trim(raw);
			if (!StartsWith(line, "- ") && !StartsWith(line, "* "))
				continue;

			const size_t open = line.find('[');
			const size_t close = line.find("](", open == std::string::npos ? 0 : open);
			const size_t end = (close == std::string::npos) ? std::string::npos : line.find(')', close);
			if (open == std::string::npos || close == std::string::npos || end == std::string::npos)
				continue;

			Page page;
			page.NavTitle = line.substr(open + 1, close - open - 1);
			page.NavDepth = (int)(IndentOf(raw) / 2);

			std::string target = Trim(line.substr(close + 2, end - close - 2));
			page.Source = root / target;
			if (target.size() > 3 && target.compare(target.size() - 3, 3, ".md") == 0)
				target = target.substr(0, target.size() - 3);
			page.Output = target + ".html";

			pages.push_back(std::move(page));
		}

		ok = !pages.empty();
		if (pages.empty())
			RV_CORE_ERROR("SUMMARY.md lists no pages");
		return pages;
	}

	// "guide/scripting.html" is two levels down, so it reaches the stylesheet
	// with "../". Relative paths mean the site works from a folder, off a
	// branch, or behind any prefix -- no base URL to configure and get wrong.
	std::string RootPrefix(const std::string& output)
	{
		std::string prefix;
		for (char c : output)
		{
			if (c == '/')
				prefix += "../";
		}
		return prefix;
	}

	// "scripting/index.html" + "../concepts.md#play-mode" -> "concepts.md#play-mode".
	//
	// Links are written relative to the page they appear on, because that is
	// what a browser follows and what GitHub renders. The checker has to do the
	// same arithmetic to know whether they land anywhere.
	std::string ResolveRelative(const std::string& fromOutput, const std::string& target)
	{
		const size_t slash = fromOutput.find_last_of('/');
		const std::string directory = (slash == std::string::npos) ? "" : fromOutput.substr(0, slash + 1);

		std::vector<std::string> parts;
		std::istringstream stream(directory + target);
		std::string part;
		while (std::getline(stream, part, '/'))
		{
			if (part.empty() || part == ".")
				continue;
			if (part == "..")
			{
				if (!parts.empty())
					parts.pop_back();
				continue;
			}
			parts.push_back(part);
		}

		std::string out;
		for (size_t i = 0; i < parts.size(); i++)
			out += (i ? "/" : "") + parts[i];
		return out;
	}

	std::string RenderNav(const std::vector<Page>& pages, size_t current)
	{
		const std::string prefix = RootPrefix(pages[current].Output);
		std::string out = "<ul class=\"nav-list\">";
		int openDepth = 0;

		for (size_t i = 0; i < pages.size(); i++)
		{
			const Page& page = pages[i];
			while (openDepth < page.NavDepth) { out += "<li><ul class=\"nav-sub\">"; openDepth++; }
			while (openDepth > page.NavDepth) { out += "</ul></li>"; openDepth--; }

			out += "<li><a class=\"nav-link";
			if (i == current)
				out += " is-current";
			out += "\" href=\"" + Escape(prefix + page.Output) + "\"";
			if (i == current)
				out += " aria-current=\"page\"";
			out += ">" + Escape(page.NavTitle) + "</a></li>";
		}

		while (openDepth-- > 0)
			out += "</ul></li>";
		out += "</ul>";
		return out;
	}

	std::string RenderToc(const Rendered& content)
	{
		// Only h2 and h3. A rail listing every h4 stops being a map.
		std::string out;
		for (const Heading& heading : content.Headings)
		{
			if (heading.Level < 2 || heading.Level > 3)
				continue;
			out += "<a class=\"toc-link toc-l" + std::to_string(heading.Level) + "\" href=\"#" +
				   Escape(heading.Anchor) + "\">" + Escape(heading.Text) + "</a>";
		}
		if (out.empty())
			return {};
		return "<p class=\"toc-title\">On this page</p><nav class=\"toc-links\">" + out + "</nav>";
	}

	std::string RenderPager(const std::vector<Page>& pages, size_t current)
	{
		const std::string prefix = RootPrefix(pages[current].Output);
		std::string out = "<nav class=\"pager\">";

		if (current > 0)
		{
			out += "<a class=\"pager-link pager-prev\" href=\"" + Escape(prefix + pages[current - 1].Output) +
				   "\"><span class=\"pager-label\">Previous</span><span class=\"pager-title\">" +
				   Escape(pages[current - 1].NavTitle) + "</span></a>";
		}
		if (current + 1 < pages.size())
		{
			out += "<a class=\"pager-link pager-next\" href=\"" + Escape(prefix + pages[current + 1].Output) +
				   "\"><span class=\"pager-label\">Next</span><span class=\"pager-title\">" +
				   Escape(pages[current + 1].NavTitle) + "</span></a>";
		}

		out += "</nav>";
		return out;
	}

	const char* kStyle = R"CSS(
/* RageV developer manual.
 *
 * The accent is red, and it obeys one rule: red marks where you are and what
 * responds to you -- the current page, a focused control, a link under the
 * cursor, and the one callout that means "this fails silently". Prose links are
 * underlined rather than coloured, because a paragraph with six red words in it
 * has stopped signalling anything.
 */
:root {
	--bg: #0b0b0d;
	--bg-raised: #131317;
	--bg-sunken: #08080a;
	--line: #26262e;
	--text: #e8e8ec;
	--text-dim: #a0a0ac;
	--text-faint: #6e6e7a;
	--accent: #e03030;
	--accent-soft: rgba(224, 48, 48, 0.14);
	--radius: 8px;
	--sidebar: 264px;
	--toc: 208px;
	--measure: 44rem;
	--mono: ui-monospace, "Cascadia Mono", "JetBrains Mono", Consolas, monospace;
	--sans: system-ui, -apple-system, "Segoe UI", Inter, sans-serif;
}
:root[data-theme="light"] {
	--bg: #ffffff;
	--bg-raised: #f6f6f8;
	--bg-sunken: #f0f0f3;
	--line: #e0e0e6;
	--text: #16161a;
	--text-dim: #55555f;
	--text-faint: #86868f;
	--accent: #c81e1e;
	--accent-soft: rgba(200, 30, 30, 0.09);
}

* { box-sizing: border-box; }
html { scroll-behavior: smooth; scroll-padding-top: 5rem; }
body {
	margin: 0;
	background: var(--bg);
	color: var(--text);
	font-family: var(--sans);
	font-size: 16px;
	line-height: 1.65;
	-webkit-font-smoothing: antialiased;
}

.skip {
	position: absolute; left: -9999px; top: 0; z-index: 100;
	background: var(--accent); color: #fff; padding: 0.6rem 1rem;
}
.skip:focus { left: 0; }

/* --- top bar --- */
.topbar {
	position: sticky; top: 0; z-index: 30;
	display: flex; align-items: center; gap: 1rem;
	height: 3.5rem; padding: 0 1.25rem;
	background: color-mix(in srgb, var(--bg) 88%, transparent);
	backdrop-filter: blur(12px);
	border-bottom: 1px solid var(--line);
}
.brand {
	font-weight: 650; font-size: 1.05rem; letter-spacing: -0.01em;
	color: var(--text); text-decoration: none; white-space: nowrap;
}
.brand-mark { color: var(--accent); }
.brand-sub {
	color: var(--text-faint); font-size: 0.8rem; white-space: nowrap;
	border-left: 1px solid var(--line); padding-left: 1rem;
}
.topbar-spacer { flex: 1; }

.icon-button {
	display: inline-flex; align-items: center; justify-content: center;
	width: 2rem; height: 2rem; padding: 0;
	background: none; border: 1px solid transparent; border-radius: var(--radius);
	color: var(--text-dim); cursor: pointer; font-size: 0.95rem;
}
.icon-button:hover { color: var(--accent); border-color: var(--line); }

/* --- search --- */
.search { position: relative; width: min(22rem, 40vw); }
.search input {
	width: 100%; padding: 0.4rem 0.7rem;
	background: var(--bg-sunken); color: var(--text);
	border: 1px solid var(--line); border-radius: var(--radius);
	font-family: inherit; font-size: 0.875rem;
}
.search input::placeholder { color: var(--text-faint); }
.search input:focus { outline: none; border-color: var(--accent); }
.results {
	position: absolute; top: calc(100% + 0.4rem); left: 0; right: 0;
	max-height: 60vh; overflow-y: auto;
	background: var(--bg-raised); border: 1px solid var(--line);
	border-radius: var(--radius); box-shadow: 0 12px 32px rgba(0,0,0,0.4);
	display: none;
}
.results.is-open { display: block; }
.result {
	display: block; padding: 0.55rem 0.8rem;
	color: var(--text); text-decoration: none;
	border-bottom: 1px solid var(--line);
}
.result:last-child { border-bottom: none; }
.result:hover, .result.is-active { background: var(--accent-soft); }
.result-title { font-size: 0.875rem; font-weight: 550; }
.result-page { font-size: 0.75rem; color: var(--text-faint); }
.result-empty { padding: 0.8rem; color: var(--text-faint); font-size: 0.85rem; }

/* --- layout --- */
.layout {
	display: grid;
	grid-template-columns: var(--sidebar) minmax(0, 1fr) var(--toc);
	gap: 2.5rem;
	max-width: 96rem; margin: 0 auto; padding: 0 1.25rem;
}
.sidebar {
	position: sticky; top: 3.5rem; align-self: start;
	max-height: calc(100vh - 3.5rem); overflow-y: auto;
	padding: 1.5rem 0.5rem 3rem 0;
}
.nav-list, .nav-sub { list-style: none; margin: 0; padding: 0; }
.nav-sub { margin-left: 0.75rem; border-left: 1px solid var(--line); padding-left: 0.5rem; }
.nav-link {
	display: block; padding: 0.28rem 0.6rem; margin: 1px 0;
	color: var(--text-dim); text-decoration: none;
	font-size: 0.875rem; border-radius: 6px;
}
.nav-link:hover { color: var(--text); background: var(--bg-raised); }
.nav-link.is-current { color: var(--accent); background: var(--accent-soft); font-weight: 550; }

main { min-width: 0; padding: 2rem 0 5rem; }
article { max-width: var(--measure); }

.toc {
	position: sticky; top: 3.5rem; align-self: start;
	max-height: calc(100vh - 3.5rem); overflow-y: auto;
	padding: 2.4rem 0 3rem;
}
.toc-title {
	margin: 0 0 0.5rem; font-size: 0.72rem; font-weight: 600;
	letter-spacing: 0.08em; text-transform: uppercase; color: var(--text-faint);
}
.toc-links { display: flex; flex-direction: column; gap: 0.1rem; }
.toc-link {
	color: var(--text-dim); text-decoration: none; font-size: 0.8rem;
	padding: 0.15rem 0 0.15rem 0.7rem; border-left: 2px solid var(--line);
}
.toc-link:hover { color: var(--text); }
.toc-link.is-active { color: var(--accent); border-left-color: var(--accent); }
.toc-l3 { padding-left: 1.5rem; }

/* --- prose --- */
h1, h2, h3, h4 { line-height: 1.25; letter-spacing: -0.015em; scroll-margin-top: 5rem; }
h1 { font-size: 2.1rem; margin: 0 0 1.5rem; font-weight: 680; }
h2 {
	font-size: 1.4rem; margin: 3rem 0 1rem; font-weight: 620;
	padding-bottom: 0.4rem; border-bottom: 1px solid var(--line);
}
h3 { font-size: 1.1rem; margin: 2rem 0 0.75rem; font-weight: 600; }
h4 { font-size: 0.95rem; margin: 1.5rem 0 0.5rem; font-weight: 600; color: var(--text-dim); }

.anchor {
	margin-left: 0.5rem; color: var(--text-faint); text-decoration: none;
	opacity: 0; font-weight: 400;
}
h1:hover .anchor, h2:hover .anchor, h3:hover .anchor, h4:hover .anchor { opacity: 1; }
.anchor:hover { color: var(--accent); }

p { margin: 0 0 1rem; }
a { color: inherit; text-decoration: underline; text-decoration-color: var(--text-faint); text-underline-offset: 3px; }
a:hover { color: var(--accent); text-decoration-color: var(--accent); }
strong { font-weight: 620; }

ul, ol { margin: 0 0 1rem; padding-left: 1.4rem; }
li { margin: 0.3rem 0; }
li > ul, li > ol { margin: 0.3rem 0; }

code {
	font-family: var(--mono); font-size: 0.86em;
	background: var(--bg-raised); border: 1px solid var(--line);
	border-radius: 5px; padding: 0.1em 0.35em;
}

figure.code {
	margin: 1.25rem 0; background: var(--bg-sunken);
	border: 1px solid var(--line); border-radius: var(--radius); overflow: hidden;
}
figure.code figcaption {
	padding: 0.35rem 0.9rem; font-family: var(--mono); font-size: 0.7rem;
	letter-spacing: 0.06em; text-transform: uppercase; color: var(--text-faint);
	background: var(--bg-raised); border-bottom: 1px solid var(--line);
}
figure.code pre { margin: 0; padding: 0.9rem 1rem; overflow-x: auto; }
figure.code code {
	background: none; border: none; padding: 0;
	font-size: 0.82rem; line-height: 1.6;
}

.tok-comment { color: var(--text-faint); font-style: italic; }
.tok-keyword { color: #e05a5a; }
.tok-type    { color: #d8a657; }
.tok-string  { color: #8fbf7f; }
.tok-number  { color: #c39ac9; }
.tok-call    { color: #7fb3d5; }
.tok-pre     { color: #9c8fbf; }
:root[data-theme="light"] .tok-keyword { color: #b02222; }
:root[data-theme="light"] .tok-type    { color: #8a6100; }
:root[data-theme="light"] .tok-string  { color: #2f6b2f; }
:root[data-theme="light"] .tok-number  { color: #7a3d8a; }
:root[data-theme="light"] .tok-call    { color: #1f5c85; }
:root[data-theme="light"] .tok-pre     { color: #5a4a8a; }

blockquote {
	margin: 1.25rem 0; padding: 0.1rem 0 0.1rem 1rem;
	border-left: 3px solid var(--line); color: var(--text-dim);
}

.callout {
	margin: 1.25rem 0; padding: 0.9rem 1.1rem;
	background: var(--bg-raised); border: 1px solid var(--line);
	border-left-width: 3px; border-radius: var(--radius);
}
.callout > :last-child { margin-bottom: 0; }
.callout-label {
	margin: 0 0 0.35rem; font-size: 0.72rem; font-weight: 650;
	letter-spacing: 0.08em; text-transform: uppercase;
}
.callout-note    { border-left-color: #5b8fd6; }
.callout-note    .callout-label { color: #5b8fd6; }
.callout-tip     { border-left-color: #5fa878; }
.callout-tip     .callout-label { color: #5fa878; }
.callout-warning { border-left-color: #d6a13a; }
.callout-warning .callout-label { color: #d6a13a; }
.callout-trap    { border-left-color: var(--accent); background: var(--accent-soft); }
.callout-trap    .callout-label { color: var(--accent); }

.table-scroll { overflow-x: auto; margin: 1.25rem 0; }
table { border-collapse: collapse; width: 100%; font-size: 0.875rem; }
th, td { padding: 0.5rem 0.75rem; border-bottom: 1px solid var(--line); vertical-align: top; }
th { font-weight: 600; color: var(--text-dim); background: var(--bg-raised); }
tbody tr:hover { background: var(--bg-raised); }

hr { border: none; border-top: 1px solid var(--line); margin: 2.5rem 0; }

/* --- pager --- */
.pager {
	display: flex; gap: 1rem; margin-top: 4rem;
	padding-top: 1.5rem; border-top: 1px solid var(--line);
	max-width: var(--measure);
}
.pager-link {
	flex: 1; display: flex; flex-direction: column; gap: 0.15rem;
	padding: 0.8rem 1rem; border: 1px solid var(--line);
	border-radius: var(--radius); text-decoration: none;
}
.pager-link:hover { border-color: var(--accent); }
.pager-next { text-align: right; }
.pager-label { font-size: 0.72rem; color: var(--text-faint); text-transform: uppercase; letter-spacing: 0.07em; }
.pager-title { font-size: 0.9rem; font-weight: 550; }

/* --- narrow --- */
.nav-toggle { display: none; }
@media (max-width: 1180px) {
	.layout { grid-template-columns: var(--sidebar) minmax(0, 1fr); }
	.toc { display: none; }
}
@media (max-width: 860px) {
	.layout { grid-template-columns: minmax(0, 1fr); gap: 0; }
	.nav-toggle { display: inline-flex; }
	.brand-sub { display: none; }
	.sidebar {
		display: none; position: fixed; inset: 3.5rem 0 0 0; z-index: 25;
		max-height: none; padding: 1rem 1.25rem 3rem;
		background: var(--bg); border-right: none;
	}
	.sidebar.is-open { display: block; }
	.search { width: auto; flex: 1; }
	h1 { font-size: 1.7rem; }
}
)CSS";

	const char* kScript = R"JS(
(function () {
	var root = document.documentElement;

	// --- theme ---
	// Dark by default because that is what this engine looks like, but a manual
	// gets read for an hour at a time and that is not everyone's preference.
	var stored = null;
	try { stored = localStorage.getItem("rvdoc-theme"); } catch (e) {}
	if (stored) root.setAttribute("data-theme", stored);

	var themeButton = document.getElementById("theme");
	if (themeButton) {
		themeButton.addEventListener("click", function () {
			var next = root.getAttribute("data-theme") === "light" ? "dark" : "light";
			root.setAttribute("data-theme", next);
			try { localStorage.setItem("rvdoc-theme", next); } catch (e) {}
		});
	}

	var navToggle = document.getElementById("nav-toggle");
	var sidebar = document.querySelector(".sidebar");
	if (navToggle && sidebar) {
		navToggle.addEventListener("click", function () { sidebar.classList.toggle("is-open"); });
	}

	// --- on this page ---
	// Highlights the section actually in view rather than the last one clicked,
	// which is the only version that stays right when someone scrolls.
	var tocLinks = Array.prototype.slice.call(document.querySelectorAll(".toc-link"));
	if (tocLinks.length && "IntersectionObserver" in window) {
		var byId = {};
		tocLinks.forEach(function (link) { byId[link.getAttribute("href").slice(1)] = link; });

		var visible = {};
		var observer = new IntersectionObserver(function (entries) {
			entries.forEach(function (entry) { visible[entry.target.id] = entry.isIntersecting; });
			var current = null;
			Object.keys(byId).forEach(function (id) {
				if (visible[id] && !current) current = id;
			});
			tocLinks.forEach(function (link) { link.classList.remove("is-active"); });
			if (current && byId[current]) byId[current].classList.add("is-active");
		}, { rootMargin: "-72px 0px -70% 0px" });

		Object.keys(byId).forEach(function (id) {
			var heading = document.getElementById(id);
			if (heading) observer.observe(heading);
		});
	}

	// --- search ---
	// The index is a script rather than a fetched file on purpose: fetch() is
	// blocked on file://, and opening the manual from a folder has to work.
	var input = document.getElementById("search-input");
	var results = document.getElementById("search-results");
	var index = window.RVDOC_INDEX || [];
	var prefix = root.getAttribute("data-root") || "";
	var active = -1;

	function close() {
		results.classList.remove("is-open");
		results.innerHTML = "";
		active = -1;
	}

	function run(query) {
		var terms = query.toLowerCase().split(/\s+/).filter(Boolean);
		if (!terms.length) { close(); return; }

		var hits = [];
		for (var i = 0; i < index.length; i++) {
			var entry = index[i];
			var haystack = entry.h.toLowerCase() + " " + entry.p.toLowerCase() + " " + entry.t.toLowerCase();
			var score = 0, matchedAll = true;
			for (var t = 0; t < terms.length; t++) {
				var at = haystack.indexOf(terms[t]);
				if (at < 0) { matchedAll = false; break; }
				// A hit in a heading beats a hit buried in a paragraph.
				score += entry.h.toLowerCase().indexOf(terms[t]) >= 0 ? 10 : 1;
			}
			if (matchedAll) hits.push({ entry: entry, score: score });
		}

		hits.sort(function (a, b) { return b.score - a.score; });
		hits = hits.slice(0, 12);

		if (!hits.length) {
			results.innerHTML = '<p class="result-empty">Nothing matches that.</p>';
			results.classList.add("is-open");
			return;
		}

		results.innerHTML = hits.map(function (hit) {
			var e = hit.entry;
			return '<a class="result" href="' + prefix + e.u + '">' +
				   '<span class="result-title">' + e.h + '</span><br>' +
				   '<span class="result-page">' + e.p + '</span></a>';
		}).join("");
		results.classList.add("is-open");
		active = -1;
	}

	if (input && results) {
		input.addEventListener("input", function () { run(input.value); });
		input.addEventListener("blur", function () { setTimeout(close, 150); });
		input.addEventListener("keydown", function (event) {
			var items = results.querySelectorAll(".result");
			if (event.key === "Escape") { input.blur(); close(); return; }
			if (!items.length) return;
			if (event.key === "ArrowDown" || event.key === "ArrowUp") {
				event.preventDefault();
				active += event.key === "ArrowDown" ? 1 : -1;
				if (active < 0) active = items.length - 1;
				if (active >= items.length) active = 0;
				for (var i = 0; i < items.length; i++) items[i].classList.toggle("is-active", i === active);
				items[active].scrollIntoView({ block: "nearest" });
			} else if (event.key === "Enter" && active >= 0) {
				event.preventDefault();
				window.location.href = items[active].getAttribute("href");
			}
		});

		document.addEventListener("keydown", function (event) {
			if (event.key === "/" && document.activeElement !== input) {
				event.preventDefault();
				input.focus();
			}
		});
	}
})();
)JS";

	std::string RenderPage(const std::vector<Page>& pages, size_t current)
	{
		const Page& page = pages[current];
		const std::string prefix = RootPrefix(page.Output);
		const std::string title = page.Content.Title.empty() ? page.NavTitle : page.Content.Title;
		const std::string toc = RenderToc(page.Content);

		std::string html;
		html += "<!doctype html>\n<html lang=\"en\" data-theme=\"dark\" data-root=\"" + Escape(prefix) + "\">\n<head>\n";
		html += "<meta charset=\"utf-8\">\n";
		html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
		html += "<title>" + Escape(title) + " — RageV manual</title>\n";
		html += "<link rel=\"stylesheet\" href=\"" + Escape(prefix) + "style.css\">\n";
		html += "</head>\n<body>\n";
		html += "<a class=\"skip\" href=\"#content\">Skip to content</a>\n";

		html += "<header class=\"topbar\">";
		html += "<button class=\"icon-button nav-toggle\" id=\"nav-toggle\" aria-label=\"Toggle navigation\">&#9776;</button>";
		html += "<a class=\"brand\" href=\"" + Escape(prefix) + pages[0].Output + "\"><span class=\"brand-mark\">Rage</span>V</a>";
		html += "<span class=\"brand-sub\">developer manual</span>";
		html += "<span class=\"topbar-spacer\"></span>";
		html += "<div class=\"search\"><input id=\"search-input\" type=\"search\" placeholder=\"Search the manual  /\" "
				"autocomplete=\"off\" spellcheck=\"false\" aria-label=\"Search\">"
				"<div class=\"results\" id=\"search-results\"></div></div>";
		html += "<button class=\"icon-button\" id=\"theme\" aria-label=\"Toggle theme\">&#9681;</button>";
		html += "</header>\n";

		html += "<div class=\"layout\">\n";
		html += "<nav class=\"sidebar\" aria-label=\"Manual\">" + RenderNav(pages, current) + "</nav>\n";
		html += "<main id=\"content\">\n<article>\n";
		if (page.Content.Title.empty())
			html += "<h1>" + Escape(page.NavTitle) + "</h1>\n";
		html += page.Content.Html;
		html += "</article>\n" + RenderPager(pages, current) + "\n</main>\n";
		html += "<aside class=\"toc\">" + toc + "</aside>\n";
		html += "</div>\n";

		html += "<script src=\"" + Escape(prefix) + "search-index.js\"></script>\n";
		html += "<script src=\"" + Escape(prefix) + "manual.js\"></script>\n";
		html += "</body>\n</html>\n";
		return html;
	}

	// One entry per heading, carrying the surrounding page's name. Headings are
	// what people search for -- "impulse", "trigger", "fixed step" -- and a hit
	// that lands on the right section beats one that lands on the right page.
	std::string RenderSearchIndex(const std::vector<Page>& pages)
	{
		std::string out = "window.RVDOC_INDEX = [\n";
		for (const Page& page : pages)
		{
			const std::string title = page.Content.Title.empty() ? page.NavTitle : page.Content.Title;

			out += "{\"h\":\"" + EscapeJs(title) + "\",\"p\":\"" + EscapeJs(page.NavTitle) +
				   "\",\"u\":\"" + EscapeJs(page.Output) + "\",\"t\":\"" +
				   EscapeJs(page.Content.SearchText.substr(0, 1200)) + "\"},\n";

			for (const Heading& heading : page.Content.Headings)
			{
				if (heading.Level > 3)
					continue;
				out += "{\"h\":\"" + EscapeJs(heading.Text) + "\",\"p\":\"" + EscapeJs(title) +
					   "\",\"u\":\"" + EscapeJs(page.Output + "#" + heading.Anchor) + "\",\"t\":\"\"},\n";
			}
		}
		out += "];\n";
		return out;
	}

	// --- the reference is checked against the header -------------------------
	//
	// The whole reason this generator is a C++ program in the engine's own
	// repository rather than a Markdown renderer somebody downloaded. A
	// reference page is the part of a manual that goes wrong silently: the
	// header gains a method, nobody documents it, and six months later the
	// manual is describing an engine that no longer exists. Reading both and
	// comparing them turns that from a discipline into a build failure.

	// Comments name methods constantly -- this header's own prose mentions
	// AddImpulse and OnUpdate -- so they have to go before anything is parsed.
	std::string StripComments(const std::string& source)
	{
		std::string out;
		out.reserve(source.size());

		for (size_t i = 0; i < source.size(); )
		{
			if (source.compare(i, 2, "//") == 0)
			{
				const size_t end = source.find('\n', i);
				if (end == std::string::npos)
					break;
				i = end;                 // keep the newline: lines still line up
				continue;
			}
			if (source.compare(i, 2, "/*") == 0)
			{
				const size_t end = source.find("*/", i + 2);
				i = (end == std::string::npos) ? source.size() : end + 2;
				continue;
			}
			out += source[i];
			i++;
		}
		return out;
	}

	// The body of `class Name {` or `struct Name {`, brace-matched.
	std::string BodyOf(const std::string& source, const std::string& keyword, const std::string& name)
	{
		const std::string opening = keyword + " " + name;
		size_t at = source.find(opening);
		while (at != std::string::npos)
		{
			// Not a forward declaration and not a mention inside something else.
			const size_t brace = source.find('{', at);
			const size_t semicolon = source.find(';', at);
			if (brace != std::string::npos && (semicolon == std::string::npos || brace < semicolon))
			{
				int depth = 0;
				for (size_t i = brace; i < source.size(); i++)
				{
					if (source[i] == '{') depth++;
					else if (source[i] == '}' && --depth == 0)
						return source.substr(brace + 1, i - brace - 1);
				}
			}
			at = source.find(opening, at + opening.size());
		}
		return {};
	}

	// The identifier immediately before the first '(' on a line, which is the
	// method name for every declaration style this header uses -- including the
	// template members, whose name is on the line after the template header.
	std::string MemberNameOn(const std::string& line)
	{
		const size_t paren = line.find('(');
		if (paren == std::string::npos)
			return {};

		size_t end = paren;
		while (end > 0 && std::isspace((unsigned char)line[end - 1]))
			end--;

		size_t begin = end;
		while (begin > 0 && (std::isalnum((unsigned char)line[begin - 1]) || line[begin - 1] == '_'))
			begin--;

		if (begin == end)
			return {};

		// A destructor, and anything that is not a plain name.
		if (begin > 0 && (line[begin - 1] == '~' || line[begin - 1] == ':' || line[begin - 1] == '.'))
			return {};

		const std::string name = line.substr(begin, end - begin);
		if (!std::isalpha((unsigned char)name[0]) && name[0] != '_')
			return {};

		// Control flow and casts read exactly like a call.
		static const std::set<std::string> notMembers = {
			"if","for","while","switch","return","sizeof","static_cast","operator"
		};
		return notMembers.count(name) ? std::string() : name;
	}

	// Public and protected members only: private is not API and documenting it
	// would be wrong rather than merely unnecessary.
	std::set<std::string> MembersOf(const std::string& body, const std::string& typeName, bool fieldsInstead)
	{
		std::set<std::string> members;
		std::string access = fieldsInstead ? "public" : "private";   // struct vs class
		int depth = 0;

		for (const std::string& raw : SplitLines(body))
		{
			const std::string line = Trim(raw);

			// Only the type's own members, not those of anything nested in it.
			const int before = depth;
			for (char c : line)
			{
				if (c == '{') depth++;
				else if (c == '}') depth--;
			}

			if (line == "public:")         { access = "public";    continue; }
			if (line == "protected:")      { access = "protected"; continue; }
			if (line == "private:")        { access = "private";   continue; }
			if (access == "private" || before != 0)
				continue;

			if (fieldsInstead)
			{
				// `Entity Other;`, `float ImpactSpeed = 0.0f;`, `Vec3 Point{ 0.0f };`
				if (line.empty())
					continue;
				const size_t stop = line.find_first_of(";={");
				if (stop == std::string::npos)
					continue;

				std::string head = Trim(line.substr(0, stop));

				// A parenthesis in the *declaration* means this is a function.
				// One after it is an initialiser calling something, which is
				// still a field: `UUID ColorLut = UUID::Invalid();`.
				//
				// Testing the whole line was the older rule, and it silently
				// dropped every field whose default is a call -- so such a
				// field could never be reported as undocumented, which is the
				// one thing this parser exists to do.
				if (head.find('(') != std::string::npos)
					continue;
				const size_t space = head.find_last_of(" \t&*>");
				if (space == std::string::npos)
					continue;
				const std::string name = Trim(head.substr(space + 1));
				if (!name.empty() && (std::isupper((unsigned char)name[0])))
					members.insert(name);
				continue;
			}

			const std::string name = MemberNameOn(line);
			if (!name.empty() && name != typeName)
				members.insert(name);
		}

		return members;
	}

	// Every name in the leading cell of a reference table. Anchoring on the
	// first column rather than on any code span is what keeps the check precise:
	// the prose is free to mention `IsTrigger` or `OnCollision*` without those
	// being read as claims that a member exists.
	std::set<std::string> DocumentedNames(const std::string& markdown)
	{
		std::set<std::string> names;
		for (const std::string& raw : SplitLines(markdown))
		{
			const std::string line = Trim(raw);
			if (line.empty() || line[0] != '|' || IsTableSeparator(line))
				continue;

			const std::vector<std::string> cells = SplitRow(line);
			if (cells.empty())
				continue;

			const std::string first = Trim(cells[0]);
			if (first.size() < 3 || first.front() != '`')
				continue;

			const size_t close = first.find('`', 1);
			if (close == std::string::npos)
				continue;

			const std::string name = Trim(first.substr(1, close - 1));
			const bool plain = !name.empty() && std::all_of(name.begin(), name.end(),
				[](unsigned char c) { return std::isalnum(c) || c == '_'; });
			if (plain)
				names.insert(name);
		}
		return names;
	}

	struct ReferenceCheck
	{
		const char* Header;
		const char* Type;
		bool        Fields;
		const char* Page;
	};

	// Both directions, reported together. One header today; the managed class
	// library joins this table when it exists, which is the point of the table.
	int CheckReferences(const fs::path& root, const fs::path& manual)
	{
		static const ReferenceCheck checks[] = {
			{ "RageV/src/RageV/Scene/ScriptableEntity.h", "ScriptableEntity", false, "scripting/cpp-reference.md" },
			{ "RageV/src/RageV/Scene/ScriptableEntity.h", "Collision",        true,  "scripting/cpp-reference.md" },
			// The settings, both halves. These are the pages a reader goes to
			// when they want to know what a dial does, and the pages most
			// likely to rot: adding a field to either struct is a one-line
			// change that silently makes the manual wrong. It has already
			// happened -- the manual named anti-aliasing without ever listing
			// its modes, and every post-processing effect after depth of field
			// arrived undocumented.
			{ "RageV/src/RageV/Renderer/RenderSettings.h", "RenderSettings", true, "rendering.md" },
			{ "RageV/src/RageV/Renderer/PostSettings.h",   "PostSettings",   true, "post-processing.md" },

			// Every component, against one page. The names are unioned per page
			// before either direction is checked, so a field shared by two
			// components -- `Color`, `SortOrder` -- is satisfied by documenting
			// it once. The alternative is a page per component, which is a
			// table of contents nobody reads instead of a reference somebody
			// searches.
			//
			// `Particle` and `ScriptFieldOverrides` are deliberately absent:
			// they are the shape of one particle and the storage behind a
			// script's fields, neither of which anybody attaches to an entity.
			{ "RageV/src/RageV/Scene/Components.h", "IDComponent",                true, "components.md" },
			{ "RageV/src/RageV/Scene/Components.h", "RelationshipComponent",      true, "components.md" },
			{ "RageV/src/RageV/Scene/Components.h", "TagComponent",               true, "components.md" },
			{ "RageV/src/RageV/Scene/Components.h", "TransformComponent",         true, "components.md" },
			{ "RageV/src/RageV/Scene/Components.h", "CameraComponent",            true, "components.md" },
			{ "RageV/src/RageV/Scene/Components.h", "LightComponent",             true, "components.md" },
			{ "RageV/src/RageV/Scene/Components.h", "ReflectionProbeComponent",   true, "components.md" },
			{ "RageV/src/RageV/Scene/Components.h", "MeshComponent",              true, "components.md" },
			{ "RageV/src/RageV/Scene/Components.h", "AnimatorComponent",          true, "components.md" },
			{ "RageV/src/RageV/Scene/Components.h", "RigidBodyComponent",         true, "components.md" },
			{ "RageV/src/RageV/Scene/Components.h", "ColliderComponent",          true, "components.md" },
			{ "RageV/src/RageV/Scene/Components.h", "AudioSourceComponent",       true, "components.md" },
			{ "RageV/src/RageV/Scene/Components.h", "ParticleEmitterComponent",   true, "components.md" },
			{ "RageV/src/RageV/Scene/Components.h", "AudioListenerComponent",     true, "components.md" },
			{ "RageV/src/RageV/Scene/Components.h", "UICanvasComponent",          true, "components.md" },
			{ "RageV/src/RageV/Scene/Components.h", "UIRectComponent",            true, "components.md" },
			{ "RageV/src/RageV/Scene/Components.h", "UIImageComponent",           true, "components.md" },
			{ "RageV/src/RageV/Scene/Components.h", "UITextComponent",            true, "components.md" },
			{ "RageV/src/RageV/Scene/Components.h", "UIButtonComponent",          true, "components.md" },
			{ "RageV/src/RageV/Scene/Components.h", "WorldTextComponent",         true, "components.md" },
			{ "RageV/src/RageV/Scene/Components.h", "PrefabComponent",            true, "components.md" },
			{ "RageV/src/RageV/Scene/Components.h", "ManagedScriptComponent",     true, "components.md" },
			{ "RageV/src/RageV/Scene/Components.h", "NativeScriptComponent",      true, "components.md" },
		};

		int problems = 0;

		// A page may cover several types -- the reference documents both
		// ScriptableEntity and the Collision struct it hands you -- so the
		// declared names are unioned per page before either direction is
		// checked. Otherwise every Collision field would be reported as
		// undocumented by the ScriptableEntity pass.
		std::map<std::string, std::set<std::string>> declaredByPage;

		for (const ReferenceCheck& check : checks)
		{
			bool read = false;
			const std::string header = ReadFile(root / check.Header, read);
			if (!read)
			{
				RV_CORE_ERROR("Cannot read {0} to check the reference against", check.Header);
				problems++;
				continue;
			}

			const std::string body = BodyOf(StripComments(header), check.Fields ? "struct" : "class", check.Type);
			if (body.empty())
			{
				RV_CORE_ERROR("No {0} '{1}' found in {2} -- it was renamed or moved",
							  check.Fields ? "struct" : "class", check.Type, check.Header);
				problems++;
				continue;
			}

			const std::set<std::string> declared = MembersOf(body, check.Type, check.Fields);

			// A check that passes because it parsed nothing is worse than no
			// check: it reports success forever while the manual rots. If the
			// header ever stops looking the way this parser expects, that has
			// to be loud.
			//
			// **The threshold is one, not four.** Four was right while this
			// covered a single large class, and wrong the moment it covered
			// components: `TagComponent` has one field and `PrefabComponent`
			// has one, so a floor of four skipped eight of them *and then*
			// reported everything the page said about them as documenting
			// something that no longer exists -- a failure that reads as the
			// manual being wrong when the manual was right.
			//
			// One is still enough to catch the case this guards. A parser that
			// lost the header reads zero; a parser that half lost it leaves
			// most fields undeclared, and the other direction below then
			// reports every one of them as documented-but-absent. The two
			// directions together are the check, and neither alone was.
			if (declared.empty())
			{
				RV_CORE_ERROR("No members parsed from {0} -- the parser has lost track of the header",
							  check.Type);
				problems++;
				continue;
			}

			RV_CORE_INFO("  {0}: {1} member(s) checked against {2}",
						 check.Type, declared.size(), check.Page);
			declaredByPage[check.Page].insert(declared.begin(), declared.end());

			bool pageRead = false;
			const std::set<std::string> documented = DocumentedNames(ReadFile(manual / check.Page, pageRead));
			if (!pageRead)
			{
				RV_CORE_ERROR("Cannot read {0}", check.Page);
				problems++;
				continue;
			}

			for (const std::string& name : declared)
			{
				if (!documented.count(name))
				{
					RV_CORE_ERROR("{0}::{1} exists but {2} does not document it",
								  check.Type, name, check.Page);
					problems++;
				}
			}
		}

		// The other direction matters just as much: a reference describing a
		// method that was deleted sends people to write code that will not
		// compile, and looks authoritative while doing it.
		for (const auto& [pagePath, declared] : declaredByPage)
		{
			bool read = false;
			const std::set<std::string> documented = DocumentedNames(ReadFile(manual / pagePath, read));
			if (!read)
				continue;

			for (const std::string& name : documented)
			{
				if (!declared.count(name))
				{
					RV_CORE_ERROR("{0} documents '{1}', which no longer exists", pagePath, name);
					problems++;
				}
			}
		}

		return problems;
	}

	bool WriteIfChanged(const fs::path& path, const std::string& contents)
	{
		bool read = false;
		const std::string existing = ReadFile(path, read);
		if (read && existing == contents)
			return true;

		std::error_code ec;
		fs::create_directories(path.parent_path(), ec);

		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			RV_CORE_ERROR("Could not write {0}", path.string());
			return false;
		}
		out << contents;
		return true;
	}
}

int main(int argc, char** argv)
{
	RageV::Log::Init();

	fs::path input = "docs/manual";
	fs::path output = "build/docs";
	fs::path root = ".";                 // where the headers are checked from
	bool checkOnly = false;

	for (int i = 1; i < argc; i++)
	{
		const std::string argument = argv[i];
		if (StartsWith(argument, "--in="))         input = argument.substr(5);
		else if (StartsWith(argument, "--out="))   output = argument.substr(6);
		else if (StartsWith(argument, "--root="))  root = argument.substr(7);
		else if (argument == "--check")            checkOnly = true;
		else if (argument == "--in" && i + 1 < argc)   input = argv[++i];
		else if (argument == "--out" && i + 1 < argc)  output = argv[++i];
		else if (argument == "--root" && i + 1 < argc) root = argv[++i];
		else
		{
			RV_CORE_ERROR("Unknown argument '{0}'", argument);
			RV_CORE_INFO("Usage: rvdoc [--in docs/manual] [--out build/docs] [--root .] [--check]");
			return 1;
		}
	}

	std::error_code ec;
	if (!fs::exists(input, ec))
	{
		RV_CORE_ERROR("No manual at {0} (run from the repository root)", input.string());
		return 1;
	}

	bool ok = true;
	std::vector<Page> pages = ParseSummary(input, ok);
	if (!ok)
		return 1;

	// Every page is read before any is written, so a broken link or a missing
	// file is reported against the whole manual rather than after half of it
	// has already been emitted.
	int missing = 0;
	for (Page& page : pages)
	{
		bool read = false;
		const std::string text = ReadFile(page.Source, read);
		if (!read)
		{
			RV_CORE_ERROR("SUMMARY.md lists {0}, which does not exist", page.Source.string());
			missing++;
			continue;
		}
		page.Content = RenderMarkdown(SplitLines(text));
	}

	if (missing > 0)
	{
		RV_CORE_ERROR("{0} page(s) missing", missing);
		return 1;
	}

	// Internal links have to resolve. A manual whose cross-references 404 is
	// worse than one that never made them, and this is the check that a person
	// proof-reading their own writing reliably fails to perform.
	std::set<std::string> known;
	for (const Page& page : pages)
	{
		known.insert(page.Output);
		for (const Heading& heading : page.Content.Headings)
			known.insert(page.Output + "#" + heading.Anchor);
	}

	int broken = 0;
	for (const Page& page : pages)
	{
		bool read = false;
		const std::string text = ReadFile(page.Source, read);
		size_t at = 0;
		while ((at = text.find("](", at)) != std::string::npos)
		{
			const size_t end = text.find(')', at);
			if (end == std::string::npos)
				break;

			std::string target = Trim(text.substr(at + 2, end - at - 2));
			at = end;

			if (target.empty() || StartsWith(target, "http://") || StartsWith(target, "https://") ||
				StartsWith(target, "mailto:") || target[0] == '#')
				continue;

			const size_t hash = target.find('#');
			std::string path = (hash == std::string::npos) ? target : target.substr(0, hash);
			const std::string anchor = (hash == std::string::npos) ? "" : target.substr(hash);
			if (path.size() > 3 && path.compare(path.size() - 3, 3, ".md") == 0)
				path = path.substr(0, path.size() - 3) + ".html";
			path = ResolveRelative(page.Output, path);

			if (!known.count(path + anchor))
			{
				RV_CORE_ERROR("{0}: link to '{1}' resolves to nothing", page.Source.filename().string(), target);
				broken++;
			}
		}
	}

	if (broken > 0)
	{
		RV_CORE_ERROR("{0} broken link(s)", broken);
		return 1;
	}

	const int drifted = CheckReferences(root, input);
	if (drifted > 0)
	{
		RV_CORE_ERROR("{0} reference entr(ies) out of step with the headers", drifted);
		return 1;
	}

	if (checkOnly)
	{
		RV_CORE_INFO("{0} page(s) parse, every link resolves, the reference matches the headers", pages.size());
		return 0;
	}

	bool wrote = true;
	for (size_t i = 0; i < pages.size(); i++)
		wrote &= WriteIfChanged(output / pages[i].Output, RenderPage(pages, i));

	wrote &= WriteIfChanged(output / "style.css", kStyle);
	wrote &= WriteIfChanged(output / "manual.js", kScript);
	wrote &= WriteIfChanged(output / "search-index.js", RenderSearchIndex(pages));

	if (!wrote)
		return 1;

	size_t headings = 0;
	for (const Page& page : pages)
		headings += page.Content.Headings.size();

	RV_CORE_INFO("Wrote {0} page(s), {1} searchable section(s) to {2}",
				 pages.size(), headings, fs::absolute(output).string());
	return 0;
}
