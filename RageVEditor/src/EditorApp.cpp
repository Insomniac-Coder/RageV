//Entrypoint
#include "RageV.h"
#include "RageV/Core/Entrypoint.h"
#include "EditorLayer.h"

class RageVEditor : public RageV::Application {
public:
	// **The editor asks; nothing else does.** A packaged game ships with its
	// project beside it and a command-line tool is told which one -- the
	// editor is the only application here that can be started by somebody who
	// has not decided yet.
	RageVEditor() : Application("RageV Editor", /*choosesProject*/ true)
	{
		PushLayer(new EditorLayer());
	}
	~RageVEditor()
	{

	}
};

RageV::Application* RageV::CreateApplication() {
	RageVEditor* editor = new RageVEditor();

	return editor;
}