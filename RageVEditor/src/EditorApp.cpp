//Entrypoint
#include "RageV.h"
#include "RageV/Core/Entrypoint.h"
#include "EditorLayer.h"

class RageVEditor : public RageV::Application {
public:
	RageVEditor() : Application("RageV Editor")
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