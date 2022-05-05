#pragma once

#include "Event.h"

#include <sstream>

namespace RageV {

	class RV_API WindowResizeEvent : public Event {
	public:
		WindowResizeEvent(unsigned int width, unsigned int height): m_Width(width), m_Height(height) {}

		inline unsigned int GetWidth() const { return m_Width; }
		inline unsigned int GetHeight() const { return m_Height; }

		std::string ToString() const override {
			std::stringstream ss;
			ss << "WindowResizeEvent: " << m_Width << ", " << m_Height;

			return ss.str();
		}

		EVENT_CLASS_CATEGORY(EventCategoryApplication)
		EVENT_CLASS_TYPE(WindowResize)

	private:
		unsigned int m_Width, m_Height;
	};

#define CLASS_DEF(classname) class RV_API classname##Event : public Event {\
							 public:\
							 	classname##Event() {}\
							 	EVENT_CLASS_CATEGORY(EventCategoryApplication)\
							 	EVENT_CLASS_TYPE(##classname)\
							 };

	CLASS_DEF(WindowClose)
	CLASS_DEF(AppTick)
	CLASS_DEF(AppUpdate)
	CLASS_DEF(AppRender)
}