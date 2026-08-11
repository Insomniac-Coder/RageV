#include "AssetIcons.h"
#include "EditorTheme.h"
#include <algorithm>
#include <cctype>
#include <string>

namespace RageV::UI
{
	namespace
	{
		// Every icon is authored in a unit square and mapped on the way out, so
		// the coordinates below read as a drawing rather than as arithmetic.
		struct Canvas
		{
			ImDrawList* Draw;
			ImVec2 Origin;
			float Size;
			ImU32 Color;

			ImVec2 At(float x, float y) const
			{
				return { Origin.x + x * Size, Origin.y + y * Size };
			}

			// Scales with the icon, with a floor: below about a pixel a stroke
			// stops being a line and starts being a grey smear.
			float Stroke() const
			{
				return std::max(1.0f, Size * 0.055f);
			}

			void Line(float x1, float y1, float x2, float y2) const
			{
				Draw->AddLine(At(x1, y1), At(x2, y2), Color, Stroke());
			}

			void Path(std::initializer_list<ImVec2> unitPoints, bool closed) const
			{
				ImVec2 points[16];
				int count = 0;
				for (const ImVec2& p : unitPoints)
				{
					if (count == 16)
						break;
					points[count++] = At(p.x, p.y);
				}
				Draw->AddPolyline(points, count, Color,
								  closed ? ImDrawFlags_Closed : ImDrawFlags_None, Stroke());
			}

			void Circle(float x, float y, float radius) const
			{
				Draw->AddCircle(At(x, y), radius * Size, Color, 0, Stroke());
			}

			void Disc(float x, float y, float radius) const
			{
				Draw->AddCircleFilled(At(x, y), radius * Size, Color, 0);
			}

			void Rect(float x1, float y1, float x2, float y2, float rounding = 0.04f) const
			{
				Draw->AddRect(At(x1, y1), At(x2, y2), Color, rounding * Size,
							  ImDrawFlags_None, Stroke());
			}
		};

		// --- the seven the registry imports -------------------------------

		// The tab, then the body, as one outline so the notch where they meet
		// stays a single crisp corner.
		void Folder(const Canvas& c)
		{
			c.Path({ { 0.10f, 0.78f }, { 0.10f, 0.26f }, { 0.42f, 0.26f },
					 { 0.50f, 0.38f }, { 0.90f, 0.38f }, { 0.90f, 0.78f } }, true);
		}

		// An isometric cube in wireframe, which is what a mesh looks like in
		// the viewport with wireframe on.
		void Mesh(const Canvas& c)
		{
			c.Path({ { 0.50f, 0.16f }, { 0.86f, 0.35f }, { 0.50f, 0.54f }, { 0.14f, 0.35f } }, true);
			c.Path({ { 0.14f, 0.35f }, { 0.14f, 0.66f }, { 0.50f, 0.85f }, { 0.50f, 0.54f } }, false);
			c.Path({ { 0.86f, 0.35f }, { 0.86f, 0.66f }, { 0.50f, 0.85f } }, false);
		}

		// The universal picture frame. A sun and a ridge, because a frame on
		// its own reads as an empty box.
		void Texture(const Canvas& c)
		{
			c.Rect(0.12f, 0.22f, 0.88f, 0.78f, 0.06f);
			c.Disc(0.33f, 0.38f, 0.06f);
			c.Path({ { 0.16f, 0.72f }, { 0.38f, 0.48f }, { 0.54f, 0.63f },
					 { 0.65f, 0.53f }, { 0.84f, 0.72f } }, false);
		}

		// The preview sphere every renderer shows one on, with the specular
		// highlight that makes it read as shaded rather than as a circle.
		void Material(const Canvas& c)
		{
			c.Circle(0.50f, 0.50f, 0.32f);
			c.Disc(0.38f, 0.37f, 0.07f);
		}

		// One thing and a copy of it. Not a cube -- a cube is what Mesh already
		// is, and two icons that differ by a badge are two icons nobody tells
		// apart at 34 pixels.
		void Prefab(const Canvas& c)
		{
			c.Rect(0.12f, 0.12f, 0.62f, 0.62f, 0.06f);
			c.Rect(0.38f, 0.38f, 0.88f, 0.88f, 0.06f);
		}

		// Ground, something standing on it, and a sun. The smallest drawing
		// that reads as a world rather than an object.
		void Scene(const Canvas& c)
		{
			c.Line(0.10f, 0.66f, 0.90f, 0.66f);
			c.Rect(0.26f, 0.42f, 0.52f, 0.66f, 0.03f);
			c.Circle(0.71f, 0.36f, 0.10f);
		}

		// A waveform rather than a speaker. A speaker means output; the asset
		// is the sound itself, and a waveform is also what the file looks like
		// in every tool that opens one.
		void Audio(const Canvas& c)
		{
			constexpr float bars[][2] = {
				{ 0.18f, 0.10f }, { 0.31f, 0.22f }, { 0.44f, 0.34f },
				{ 0.57f, 0.18f }, { 0.70f, 0.27f }, { 0.83f, 0.09f },
			};
			for (const auto& bar : bars)
				c.Line(bar[0], 0.50f - bar[1], bar[0], 0.50f + bar[1]);
		}

		// The ramp itself, with its end keys. Deliberately the same shape the
		// curve editor draws, so the icon and the thing it opens look like
		// each other.
		void Curve(const Canvas& c)
		{
			c.Draw->AddBezierCubic(c.At(0.14f, 0.76f), c.At(0.42f, 0.76f),
								   c.At(0.50f, 0.26f), c.At(0.86f, 0.26f),
								   c.Color, c.Stroke(), 0);
			c.Disc(0.14f, 0.76f, 0.075f);
			c.Disc(0.86f, 0.26f, 0.075f);
		}

		// --- everything else in a project's folders ------------------------

		// Angle brackets and a slash. The most universally read "this is code"
		// mark there is, and it says nothing about which language, which is
		// correct: a .cs and a .cpp are the same kind of thing here.
		void Script(const Canvas& c)
		{
			c.Path({ { 0.36f, 0.28f }, { 0.14f, 0.50f }, { 0.36f, 0.72f } }, false);
			c.Path({ { 0.64f, 0.28f }, { 0.86f, 0.50f }, { 0.64f, 0.72f } }, false);
			c.Line(0.56f, 0.22f, 0.44f, 0.78f);
		}

		// A triangle with scanlines: the primitive a shader runs on, being
		// rasterised. Deliberately not another sphere -- Material already owns
		// the sphere, and a shader is the program rather than the surface.
		void Shader(const Canvas& c)
		{
			c.Path({ { 0.50f, 0.16f }, { 0.86f, 0.80f }, { 0.14f, 0.80f } }, true);
			c.Line(0.33f, 0.51f, 0.67f, 0.51f);
			c.Line(0.25f, 0.65f, 0.75f, 0.65f);
		}

		// A letter A with its crossbar, which is what a font is for.
		void Font(const Canvas& c)
		{
			c.Path({ { 0.22f, 0.80f }, { 0.50f, 0.18f }, { 0.78f, 0.80f } }, false);
			c.Line(0.33f, 0.58f, 0.67f, 0.58f);
		}

		// A page with a folded corner and lines of writing on it.
		void Document(const Canvas& c)
		{
			c.Path({ { 0.24f, 0.12f }, { 0.60f, 0.12f }, { 0.78f, 0.31f },
					 { 0.78f, 0.88f }, { 0.24f, 0.88f } }, true);
			c.Path({ { 0.60f, 0.12f }, { 0.60f, 0.31f }, { 0.78f, 0.31f } }, false);
			c.Line(0.34f, 0.48f, 0.68f, 0.48f);
			c.Line(0.34f, 0.61f, 0.68f, 0.61f);
			c.Line(0.34f, 0.74f, 0.56f, 0.74f);
		}

		// Braces. Every format in this bucket -- json, yaml, ini, csproj -- is
		// structured text, and braces are how structured text is drawn.
		void Data(const Canvas& c)
		{
			c.Path({ { 0.42f, 0.18f }, { 0.30f, 0.18f }, { 0.30f, 0.44f },
					 { 0.18f, 0.50f }, { 0.30f, 0.56f }, { 0.30f, 0.82f },
					 { 0.42f, 0.82f } }, false);
			c.Path({ { 0.58f, 0.18f }, { 0.70f, 0.18f }, { 0.70f, 0.44f },
					 { 0.82f, 0.50f }, { 0.70f, 0.56f }, { 0.70f, 0.82f },
					 { 0.58f, 0.82f } }, false);
		}

		// A parcel: a box with a band around it. Says "several things inside
		// one file" without needing a label.
		void Archive(const Canvas& c)
		{
			c.Rect(0.14f, 0.26f, 0.86f, 0.80f, 0.05f);
			c.Line(0.14f, 0.44f, 0.86f, 0.44f);
			c.Line(0.50f, 0.26f, 0.50f, 0.44f);
			c.Rect(0.42f, 0.52f, 0.58f, 0.64f, 0.02f);
		}

		// Nothing recognised: a plain page, no writing on it. The absence of
		// detail is the message.
		void UnknownFile(const Canvas& c)
		{
			c.Path({ { 0.24f, 0.12f }, { 0.60f, 0.12f }, { 0.78f, 0.31f },
					 { 0.78f, 0.88f }, { 0.24f, 0.88f } }, true);
			c.Path({ { 0.60f, 0.12f }, { 0.60f, 0.31f }, { 0.78f, 0.31f } }, false);
		}


		// --- what is in a scene --------------------------------------------

		// A plain entity: a node. Hollow, because an entity with nothing on it
		// is a position and a name, and the icon should look as empty as that.
		void EntityNode(const Canvas& c)
		{
			c.Path({ { 0.50f, 0.22f }, { 0.78f, 0.50f }, { 0.50f, 0.78f }, { 0.22f, 0.50f } }, true);
		}

		// A light: the sun mark. Rays rather than a bulb -- a bulb is a lamp
		// you can hold, and these light a world.
		void Light(const Canvas& c)
		{
			c.Circle(0.50f, 0.50f, 0.18f);
			constexpr float rays[][4] = {
				{ 0.50f, 0.10f, 0.50f, 0.22f }, { 0.50f, 0.78f, 0.50f, 0.90f },
				{ 0.10f, 0.50f, 0.22f, 0.50f }, { 0.78f, 0.50f, 0.90f, 0.50f },
				{ 0.22f, 0.22f, 0.31f, 0.31f }, { 0.69f, 0.69f, 0.78f, 0.78f },
				{ 0.78f, 0.22f, 0.69f, 0.31f }, { 0.31f, 0.69f, 0.22f, 0.78f },
			};
			for (const auto& ray : rays)
				c.Line(ray[0], ray[1], ray[2], ray[3]);
		}

		// A camera: the body and the lens barrel sticking out of it, which is
		// the shape a camera gizmo draws in the viewport.
		void Camera(const Canvas& c)
		{
			c.Rect(0.12f, 0.34f, 0.62f, 0.68f, 0.05f);
			c.Path({ { 0.62f, 0.44f }, { 0.86f, 0.32f }, { 0.86f, 0.70f }, { 0.62f, 0.58f } }, true);
		}

		// An emitter: a source and what has left it. The dots get smaller with
		// distance, which is the one thing every particle system does.
		void ParticleEmitter(const Canvas& c)
		{
			c.Disc(0.24f, 0.74f, 0.085f);
			c.Disc(0.46f, 0.54f, 0.060f);
			c.Disc(0.64f, 0.38f, 0.045f);
			c.Disc(0.79f, 0.26f, 0.032f);
			c.Disc(0.40f, 0.78f, 0.038f);
			c.Disc(0.62f, 0.62f, 0.030f);
		}

		// A source: a speaker, deliberately not the waveform. The waveform is
		// the audio *file*; this is the thing in the world playing it.
		void AudioSource(const Canvas& c)
		{
			c.Path({ { 0.16f, 0.40f }, { 0.28f, 0.40f }, { 0.44f, 0.24f },
					 { 0.44f, 0.76f }, { 0.28f, 0.60f }, { 0.16f, 0.60f } }, true);
			c.Path({ { 0.58f, 0.36f }, { 0.66f, 0.50f }, { 0.58f, 0.64f } }, false);
			c.Path({ { 0.72f, 0.26f }, { 0.84f, 0.50f }, { 0.72f, 0.74f } }, false);
		}

		std::string LowerExtension(const std::filesystem::path& path)
		{
			std::string extension = path.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(),
						   [](unsigned char ch) { return (char)std::tolower(ch); });
			return extension;
		}
	}

	IconKind KindForAsset(AssetType type)
	{
		switch (type)
		{
			case AssetType::Mesh:     return IconKind::Mesh;
			case AssetType::Texture:  return IconKind::Texture;
			case AssetType::Material: return IconKind::Material;
			case AssetType::Prefab:   return IconKind::Prefab;
			case AssetType::Scene:    return IconKind::Scene;
			case AssetType::Audio:    return IconKind::Audio;
			case AssetType::Curve:    return IconKind::Curve;
			default:                  return IconKind::Unknown;
		}
	}

	IconKind KindForFile(const std::filesystem::path& path, AssetType type)
	{
		// The registry wins where it has an answer. An extension table that
		// could override it would eventually disagree with the loader, and the
		// icon would be describing a file the engine reads as something else.
		if (const IconKind imported = KindForAsset(type); imported != IconKind::Unknown)
			return imported;

		const std::string extension = LowerExtension(path);

		if (extension == ".cs" || extension == ".cpp" || extension == ".h"
			|| extension == ".hpp" || extension == ".c" || extension == ".cc")
			return IconKind::Script;

		if (extension == ".rvshader" || extension == ".glsl" || extension == ".vert"
			|| extension == ".frag" || extension == ".comp" || extension == ".spv")
			return IconKind::Shader;

		if (extension == ".ttf" || extension == ".otf")
			return IconKind::Font;

		if (extension == ".md" || extension == ".txt")
			return IconKind::Document;

		if (extension == ".json" || extension == ".ini" || extension == ".yaml"
			|| extension == ".yml" || extension == ".csproj" || extension == ".rvproject"
			|| extension == ".sln")
			return IconKind::Data;

		if (extension == ".pak" || extension == ".zip")
			return IconKind::Archive;

		return IconKind::Unknown;
	}

	void DrawIcon(ImDrawList* drawList, ImVec2 topLeft, float size, IconKind kind, ImU32 color)
	{
		if (!drawList || size <= 0.0f)
			return;

		const Canvas canvas{ drawList, topLeft, size, color };

		switch (kind)
		{
			case IconKind::Folder:   Folder(canvas);   break;
			case IconKind::Mesh:     Mesh(canvas);     break;
			case IconKind::Texture:  Texture(canvas);  break;
			case IconKind::Material: Material(canvas); break;
			case IconKind::Prefab:   Prefab(canvas);   break;
			case IconKind::Scene:    Scene(canvas);    break;
			case IconKind::Audio:    Audio(canvas);    break;
			case IconKind::Curve:    Curve(canvas);    break;
			case IconKind::Script:   Script(canvas);   break;
			case IconKind::Shader:   Shader(canvas);   break;
			case IconKind::Font:     Font(canvas);     break;
			case IconKind::Document: Document(canvas); break;
			case IconKind::Data:     Data(canvas);     break;
			case IconKind::Archive:  Archive(canvas);  break;
			case IconKind::Entity:   EntityNode(canvas); break;
			case IconKind::Light:    Light(canvas);    break;
			case IconKind::Camera:   Camera(canvas);   break;
			case IconKind::ParticleEmitter: ParticleEmitter(canvas); break;
			case IconKind::AudioSource:    AudioSource(canvas);     break;
			default:                 UnknownFile(canvas); break;
		}
	}

	void DrawInlineIcon(IconKind kind, ImU32 color)
	{
		const float size = ImGui::GetTextLineHeight();
		const ImVec2 where = ImGui::GetCursorScreenPos();

		DrawIcon(ImGui::GetWindowDrawList(), where, size, kind, color);
		ImGui::Dummy({ size, size });
	}
}
