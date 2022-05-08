#include <rvpch.h>
#include "LayerStack.h"

RageV::LayerStack::LayerStack()
{
	m_LayerInsert = begin();
}

RageV::LayerStack::~LayerStack()
{
	for (auto layer : m_Layers)
		delete layer;
}

void RageV::LayerStack::PushLayer(Layer* layer)
{
	m_LayerInsert = m_Layers.emplace(m_LayerInsert, layer);
}

void RageV::LayerStack::PushOverlay(Layer* layer)
{
	m_Layers.emplace_back(layer);
}

void RageV::LayerStack::PopLayer(Layer* layer)
{
	auto it = std::find(begin(), end(), layer);

	if (it != end())
	{
		m_Layers.erase(it);
		m_LayerInsert--;
	}
}

void RageV::LayerStack::PopOverlay(Layer* layer)
{
	auto it = std::find(begin(), end(), layer);

	if (it != end())
		m_Layers.erase(it);
}
