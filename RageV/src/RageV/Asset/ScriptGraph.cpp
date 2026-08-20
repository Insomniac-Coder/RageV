#include "rvpch.h"
#include "ScriptGraph.h"

#include <algorithm>
#include <functional>

namespace RageV
{
	namespace
	{
		using P = GraphPinType;
		using Emit = GraphEmit;

		// The one table. The canvas draws a node from this and the generator
		// will emit one from it, so a node cannot look like one thing on the
		// canvas and mean another in the file.
		std::vector<GraphNodeDesc> BuildDescs()
		{
			std::vector<GraphNodeDesc> descs((size_t)GraphNodeType::Count);

			auto set = [&](GraphNodeType type, const char* name, const char* category,
						   std::vector<GraphPin> inputs, std::vector<GraphPin> outputs,
						   GraphEmit emit, const char* code, bool isEvent = false)
			{
				GraphNodeDesc& desc = descs[(size_t)type];
				desc.Type = type;
				desc.Name = name;
				desc.Category = category;
				desc.Inputs = std::move(inputs);
				desc.Outputs = std::move(outputs);
				desc.IsEvent = isEvent;
				desc.Emit = emit;
				desc.Code = code;
			};

			set(GraphNodeType::None, "None", "", {}, {},
				Emit::Special, "");

			// --- events
			set(GraphNodeType::OnCreate, "On Create", "Events",
				{}, { { "", P::Exec } },
				Emit::Event, "public override void OnCreate()", true);
			set(GraphNodeType::OnTick, "On Tick", "Events",
				{}, { { "", P::Exec }, { "Delta", P::Float } },
				Emit::Event, "public override void OnTick(float deltaTime)", true);
			set(GraphNodeType::OnFrame, "On Frame", "Events",
				{}, { { "", P::Exec }, { "Delta", P::Float } },
				Emit::Event, "public override void OnFrame(float deltaTime)", true);
			set(GraphNodeType::OnDestroy, "On Destroy", "Events",
				{}, { { "", P::Exec } },
				Emit::Event, "public override void OnDestroy()", true);
			set(GraphNodeType::OnCollisionEnter, "On Collision Enter", "Events",
				{}, { { "", P::Exec }, { "Other", P::Entity }, { "Speed", P::Float }, { "Point", P::Vec3 }, { "Normal", P::Vec3 } },
				Emit::Event, "public override void OnCollisionEnter(Collision collision)", true);
			set(GraphNodeType::OnCollisionStay, "On Collision Stay", "Events",
				{}, { { "", P::Exec }, { "Other", P::Entity }, { "Speed", P::Float }, { "Point", P::Vec3 }, { "Normal", P::Vec3 } },
				Emit::Event, "public override void OnCollisionStay(Collision collision)", true);
			set(GraphNodeType::OnCollisionExit, "On Collision Exit", "Events",
				{}, { { "", P::Exec }, { "Other", P::Entity }, { "Speed", P::Float }, { "Point", P::Vec3 }, { "Normal", P::Vec3 } },
				Emit::Event, "public override void OnCollisionExit(Collision collision)", true);
			set(GraphNodeType::OnTriggerEnter, "On Trigger Enter", "Events",
				{}, { { "", P::Exec }, { "Other", P::Entity }, { "Speed", P::Float }, { "Point", P::Vec3 }, { "Normal", P::Vec3 } },
				Emit::Event, "public override void OnTriggerEnter(Collision collision)", true);
			set(GraphNodeType::OnTriggerStay, "On Trigger Stay", "Events",
				{}, { { "", P::Exec }, { "Other", P::Entity }, { "Speed", P::Float }, { "Point", P::Vec3 }, { "Normal", P::Vec3 } },
				Emit::Event, "public override void OnTriggerStay(Collision collision)", true);
			set(GraphNodeType::OnTriggerExit, "On Trigger Exit", "Events",
				{}, { { "", P::Exec }, { "Other", P::Entity }, { "Speed", P::Float }, { "Point", P::Vec3 }, { "Normal", P::Vec3 } },
				Emit::Event, "public override void OnTriggerExit(Collision collision)", true);

			// --- flow
			set(GraphNodeType::Branch, "Branch", "Flow",
				{ { "", P::Exec }, { "Condition", P::Bool } }, { { "True", P::Exec }, { "False", P::Exec } },
				Emit::Special, "branch");
			set(GraphNodeType::Sequence, "Sequence", "Flow",
				{ { "", P::Exec } }, { { "Then 0", P::Exec }, { "Then 1", P::Exec } },
				Emit::Special, "sequence");
			set(GraphNodeType::ForLoop, "For Loop", "Flow",
				{ { "", P::Exec }, { "Count", P::Float } }, { { "Body", P::Exec }, { "Index", P::Float }, { "Completed", P::Exec } },
				Emit::Special, "forloop");
			set(GraphNodeType::WhileLoop, "While Loop", "Flow",
				{ { "", P::Exec }, { "Condition", P::Bool } }, { { "Body", P::Exec }, { "Completed", P::Exec } },
				Emit::Special, "whileloop");
			set(GraphNodeType::BreakLoop, "Break", "Flow",
				{ { "", P::Exec } }, {},
				Emit::Special, "break");

			// --- functions
			set(GraphNodeType::FunctionEntry, "Function", "Functions",
				{}, { { "", P::Exec } },
				Emit::Special, "function");
			set(GraphNodeType::CallFunction, "Call", "Functions",
				{ { "", P::Exec } }, { { "", P::Exec } },
				Emit::Special, "call");

			// --- values
			set(GraphNodeType::LiteralBool, "Bool", "Values",
				{}, { { "", P::Bool } },
				Emit::Special, "literal");
			set(GraphNodeType::LiteralFloat, "Float", "Values",
				{}, { { "", P::Float } },
				Emit::Special, "literal");
			set(GraphNodeType::LiteralVec3, "Vector 3", "Values",
				{}, { { "", P::Vec3 } },
				Emit::Special, "literal");
			set(GraphNodeType::LiteralString, "String", "Values",
				{}, { { "", P::String } },
				Emit::Special, "literal");
			set(GraphNodeType::SelfEntity, "Self", "Values",
				{}, { { "", P::Entity } },
				Emit::Expression, "Entity");

			// --- variables
			set(GraphNodeType::GetNumber, "Get Number", "Variables",
				{}, { { "", P::Float } },
				Emit::GetVariable, "float");
			set(GraphNodeType::SetNumber, "Set Number", "Variables",
				{ { "", P::Exec }, { "Value", P::Float } }, { { "", P::Exec } },
				Emit::SetVariable, "float");
			set(GraphNodeType::GetVector, "Get Vector", "Variables",
				{}, { { "", P::Vec3 } },
				Emit::GetVariable, "Vector3");
			set(GraphNodeType::SetVector, "Set Vector", "Variables",
				{ { "", P::Exec }, { "Value", P::Vec3 } }, { { "", P::Exec } },
				Emit::SetVariable, "Vector3");
			set(GraphNodeType::GetFlag, "Get Flag", "Variables",
				{}, { { "", P::Bool } },
				Emit::GetVariable, "bool");
			set(GraphNodeType::SetFlag, "Set Flag", "Variables",
				{ { "", P::Exec }, { "Value", P::Bool } }, { { "", P::Exec } },
				Emit::SetVariable, "bool");
			set(GraphNodeType::GetText, "Get Text", "Variables",
				{}, { { "", P::String } },
				Emit::GetVariable, "string");
			set(GraphNodeType::SetText, "Set Text", "Variables",
				{ { "", P::Exec }, { "Value", P::String } }, { { "", P::Exec } },
				Emit::SetVariable, "string");
			set(GraphNodeType::GetEntityVar, "Get Entity", "Variables",
				{}, { { "", P::Entity } },
				Emit::GetVariable, "Entity");
			set(GraphNodeType::SetEntityVar, "Set Entity", "Variables",
				{ { "", P::Exec }, { "Value", P::Entity } }, { { "", P::Exec } },
				Emit::SetVariable, "Entity");

			// --- containers
			set(GraphNodeType::GetNumbers, "Get Numbers", "Containers",
				{}, { { "", P::NumberList } },
				Emit::GetVariable, "List<float>");
			set(GraphNodeType::SetNumbers, "Set Numbers", "Containers",
				{ { "", P::Exec }, { "Value", P::NumberList } }, { { "", P::Exec } },
				Emit::SetVariable, "List<float>");
			set(GraphNodeType::GetEntities, "Get Entities", "Containers",
				{}, { { "", P::EntityList } },
				Emit::GetVariable, "List<Entity>");
			set(GraphNodeType::SetEntities, "Set Entities", "Containers",
				{ { "", P::Exec }, { "Value", P::EntityList } }, { { "", P::Exec } },
				Emit::SetVariable, "List<Entity>");
			set(GraphNodeType::GetNumberMap, "Get Number Map", "Containers",
				{}, { { "", P::NumberMap } },
				Emit::GetVariable, "Dictionary<string, float>");
			set(GraphNodeType::SetNumberMap, "Set Number Map", "Containers",
				{ { "", P::Exec }, { "Value", P::NumberMap } }, { { "", P::Exec } },
				Emit::SetVariable, "Dictionary<string, float>");
			set(GraphNodeType::GetEntityMap, "Get Entity Map", "Containers",
				{}, { { "", P::EntityMap } },
				Emit::GetVariable, "Dictionary<string, Entity>");
			set(GraphNodeType::SetEntityMap, "Set Entity Map", "Containers",
				{ { "", P::Exec }, { "Value", P::EntityMap } }, { { "", P::Exec } },
				Emit::SetVariable, "Dictionary<string, Entity>");
			set(GraphNodeType::NumbersAdd, "Numbers Add", "Containers",
				{ { "", P::Exec }, { "List", P::NumberList }, { "Value", P::Float } }, { { "", P::Exec } },
				Emit::Statement, "{1}.Add({2});");
			set(GraphNodeType::NumbersAt, "Numbers At", "Containers",
				{ { "List", P::NumberList }, { "Index", P::Float } }, { { "", P::Float } },
				Emit::Expression, "{0}[(int)({1})]");
			set(GraphNodeType::NumbersCount, "Numbers Count", "Containers",
				{ { "List", P::NumberList } }, { { "", P::Float } },
				Emit::Expression, "(float){0}.Count");
			set(GraphNodeType::NumbersHas, "Numbers Has", "Containers",
				{ { "List", P::NumberList }, { "Value", P::Float } }, { { "", P::Bool } },
				Emit::Expression, "{0}.Contains({1})");
			set(GraphNodeType::NumbersRemoveAt, "Numbers Remove At", "Containers",
				{ { "", P::Exec }, { "List", P::NumberList }, { "Index", P::Float } }, { { "", P::Exec } },
				Emit::Statement, "{1}.RemoveAt((int)({2}));");
			set(GraphNodeType::NumbersClear, "Numbers Clear", "Containers",
				{ { "", P::Exec }, { "List", P::NumberList } }, { { "", P::Exec } },
				Emit::Statement, "{1}.Clear();");
			set(GraphNodeType::ForEachNumber, "For Each Number", "Containers",
				{ { "", P::Exec }, { "List", P::NumberList } }, { { "Body", P::Exec }, { "Element", P::Float }, { "Index", P::Float }, { "Completed", P::Exec } },
				Emit::Special, "foreach");
			set(GraphNodeType::EntitiesAdd, "Entities Add", "Containers",
				{ { "", P::Exec }, { "List", P::EntityList }, { "Value", P::Entity } }, { { "", P::Exec } },
				Emit::Statement, "{1}.Add({2});");
			set(GraphNodeType::EntitiesAt, "Entities At", "Containers",
				{ { "List", P::EntityList }, { "Index", P::Float } }, { { "", P::Entity } },
				Emit::Expression, "{0}[(int)({1})]");
			set(GraphNodeType::EntitiesCount, "Entities Count", "Containers",
				{ { "List", P::EntityList } }, { { "", P::Float } },
				Emit::Expression, "(float){0}.Count");
			set(GraphNodeType::EntitiesRemoveAt, "Entities Remove At", "Containers",
				{ { "", P::Exec }, { "List", P::EntityList }, { "Index", P::Float } }, { { "", P::Exec } },
				Emit::Statement, "{1}.RemoveAt((int)({2}));");
			set(GraphNodeType::EntitiesClear, "Entities Clear", "Containers",
				{ { "", P::Exec }, { "List", P::EntityList } }, { { "", P::Exec } },
				Emit::Statement, "{1}.Clear();");
			set(GraphNodeType::ForEachEntity, "For Each Entity", "Containers",
				{ { "", P::Exec }, { "List", P::EntityList } }, { { "Body", P::Exec }, { "Element", P::Entity }, { "Index", P::Float }, { "Completed", P::Exec } },
				Emit::Special, "foreach");
			set(GraphNodeType::NumberMapSet, "Number Map Set", "Containers",
				{ { "", P::Exec }, { "Map", P::NumberMap }, { "Key", P::String }, { "Value", P::Float } }, { { "", P::Exec } },
				Emit::Statement, "{1}[{2}] = {3};");
			set(GraphNodeType::NumberMapGet, "Number Map Get", "Containers",
				{ { "Map", P::NumberMap }, { "Key", P::String } }, { { "", P::Float } },
				Emit::Expression, "({0}.ContainsKey({1}) ? {0}[{1}] : 0.0f)");
			set(GraphNodeType::NumberMapHas, "Number Map Has", "Containers",
				{ { "Map", P::NumberMap }, { "Key", P::String } }, { { "", P::Bool } },
				Emit::Expression, "{0}.ContainsKey({1})");
			set(GraphNodeType::NumberMapRemove, "Number Map Remove", "Containers",
				{ { "", P::Exec }, { "Map", P::NumberMap }, { "Key", P::String } }, { { "", P::Exec } },
				Emit::Statement, "{1}.Remove({2});");
			set(GraphNodeType::NumberMapCount, "Number Map Count", "Containers",
				{ { "Map", P::NumberMap } }, { { "", P::Float } },
				Emit::Expression, "(float){0}.Count");
			set(GraphNodeType::NumberMapClear, "Number Map Clear", "Containers",
				{ { "", P::Exec }, { "Map", P::NumberMap } }, { { "", P::Exec } },
				Emit::Statement, "{1}.Clear();");
			set(GraphNodeType::EntityMapSet, "Entity Map Set", "Containers",
				{ { "", P::Exec }, { "Map", P::EntityMap }, { "Key", P::String }, { "Value", P::Entity } }, { { "", P::Exec } },
				Emit::Statement, "{1}[{2}] = {3};");
			set(GraphNodeType::EntityMapGet, "Entity Map Get", "Containers",
				{ { "Map", P::EntityMap }, { "Key", P::String } }, { { "", P::Entity } },
				Emit::Expression, "({0}.ContainsKey({1}) ? {0}[{1}] : Entity.Invalid)");
			set(GraphNodeType::EntityMapHas, "Entity Map Has", "Containers",
				{ { "Map", P::EntityMap }, { "Key", P::String } }, { { "", P::Bool } },
				Emit::Expression, "{0}.ContainsKey({1})");
			set(GraphNodeType::EntityMapRemove, "Entity Map Remove", "Containers",
				{ { "", P::Exec }, { "Map", P::EntityMap }, { "Key", P::String } }, { { "", P::Exec } },
				Emit::Statement, "{1}.Remove({2});");
			set(GraphNodeType::EntityMapCount, "Entity Map Count", "Containers",
				{ { "Map", P::EntityMap } }, { { "", P::Float } },
				Emit::Expression, "(float){0}.Count");
			set(GraphNodeType::EntityMapClear, "Entity Map Clear", "Containers",
				{ { "", P::Exec }, { "Map", P::EntityMap } }, { { "", P::Exec } },
				Emit::Statement, "{1}.Clear();");

			// --- maths
			set(GraphNodeType::Add, "Add", "Maths",
				{ { "A", P::Float }, { "B", P::Float } }, { { "", P::Float } },
				Emit::Expression, "({0} + {1})");
			set(GraphNodeType::Subtract, "Subtract", "Maths",
				{ { "A", P::Float }, { "B", P::Float } }, { { "", P::Float } },
				Emit::Expression, "({0} - {1})");
			set(GraphNodeType::Multiply, "Multiply", "Maths",
				{ { "A", P::Float }, { "B", P::Float } }, { { "", P::Float } },
				Emit::Expression, "({0} * {1})");
			set(GraphNodeType::Divide, "Divide", "Maths",
				{ { "A", P::Float }, { "B", P::Float } }, { { "", P::Float } },
				Emit::Expression, "({0} / {1})");
			set(GraphNodeType::Compare, "Compare", "Maths",
				{ { "A", P::Float }, { "B", P::Float } }, { { "", P::Bool } },
				Emit::Special, "compare");
			set(GraphNodeType::MinOf, "Min", "Maths",
				{ { "A", P::Float }, { "B", P::Float } }, { { "", P::Float } },
				Emit::Expression, "Mathf.Min({0}, {1})");
			set(GraphNodeType::MaxOf, "Max", "Maths",
				{ { "A", P::Float }, { "B", P::Float } }, { { "", P::Float } },
				Emit::Expression, "Mathf.Max({0}, {1})");
			set(GraphNodeType::AbsOf, "Abs", "Maths",
				{ { "Value", P::Float } }, { { "", P::Float } },
				Emit::Expression, "Mathf.Abs({0})");
			set(GraphNodeType::ClampOf, "Clamp", "Maths",
				{ { "Value", P::Float }, { "Min", P::Float }, { "Max", P::Float } }, { { "", P::Float } },
				Emit::Expression, "Mathf.Clamp({0}, {1}, {2})");
			set(GraphNodeType::LerpOf, "Lerp", "Maths",
				{ { "A", P::Float }, { "B", P::Float }, { "T", P::Float } }, { { "", P::Float } },
				Emit::Expression, "Mathf.Lerp({0}, {1}, {2})");
			set(GraphNodeType::SinOf, "Sin", "Maths",
				{ { "Value", P::Float } }, { { "", P::Float } },
				Emit::Expression, "Mathf.Sin({0})");
			set(GraphNodeType::CosOf, "Cos", "Maths",
				{ { "Value", P::Float } }, { { "", P::Float } },
				Emit::Expression, "Mathf.Cos({0})");

			// --- logic
			set(GraphNodeType::AndOf, "And", "Logic",
				{ { "A", P::Bool }, { "B", P::Bool } }, { { "", P::Bool } },
				Emit::Expression, "({0} && {1})");
			set(GraphNodeType::OrOf, "Or", "Logic",
				{ { "A", P::Bool }, { "B", P::Bool } }, { { "", P::Bool } },
				Emit::Expression, "({0} || {1})");
			set(GraphNodeType::NotOf, "Not", "Logic",
				{ { "Value", P::Bool } }, { { "", P::Bool } },
				Emit::Expression, "(!{0})");

			// --- vector
			set(GraphNodeType::MakeVector, "Make Vector", "Vector",
				{ { "X", P::Float }, { "Y", P::Float }, { "Z", P::Float } }, { { "", P::Vec3 } },
				Emit::Expression, "new Vector3({0}, {1}, {2})");
			set(GraphNodeType::BreakVectorX, "Vector X", "Vector",
				{ { "Vector", P::Vec3 } }, { { "", P::Float } },
				Emit::Expression, "{0}.X");
			set(GraphNodeType::BreakVectorY, "Vector Y", "Vector",
				{ { "Vector", P::Vec3 } }, { { "", P::Float } },
				Emit::Expression, "{0}.Y");
			set(GraphNodeType::BreakVectorZ, "Vector Z", "Vector",
				{ { "Vector", P::Vec3 } }, { { "", P::Float } },
				Emit::Expression, "{0}.Z");
			set(GraphNodeType::VectorAdd, "Vector Add", "Vector",
				{ { "A", P::Vec3 }, { "B", P::Vec3 } }, { { "", P::Vec3 } },
				Emit::Expression, "({0} + {1})");
			set(GraphNodeType::VectorSubtract, "Vector Subtract", "Vector",
				{ { "A", P::Vec3 }, { "B", P::Vec3 } }, { { "", P::Vec3 } },
				Emit::Expression, "({0} - {1})");
			set(GraphNodeType::VectorScale, "Vector Scale", "Vector",
				{ { "Vector", P::Vec3 }, { "By", P::Float } }, { { "", P::Vec3 } },
				Emit::Expression, "({0} * {1})");
			set(GraphNodeType::VectorLength, "Vector Length", "Vector",
				{ { "Vector", P::Vec3 } }, { { "", P::Float } },
				Emit::Expression, "{0}.Length");
			set(GraphNodeType::VectorNormalize, "Normalize", "Vector",
				{ { "Vector", P::Vec3 } }, { { "", P::Vec3 } },
				Emit::Expression, "{0}.Normalized");
			set(GraphNodeType::VectorDot, "Dot", "Vector",
				{ { "A", P::Vec3 }, { "B", P::Vec3 } }, { { "", P::Float } },
				Emit::Expression, "Vector3.Dot({0}, {1})");

			// --- entity
			set(GraphNodeType::FindByName, "Find By Name", "Entity",
				{ { "Name", P::String } }, { { "", P::Entity } },
				Emit::Expression, "Entity.FindByName({0})");
			set(GraphNodeType::SpawnEntity, "Spawn", "Entity",
				{ { "", P::Exec }, { "Name", P::String } }, { { "", P::Exec }, { "Entity", P::Entity } },
				Emit::Special, "spawn");
			set(GraphNodeType::SpawnPrefab, "Spawn Prefab", "Entity",
				{ { "", P::Exec }, { "Asset", P::String } }, { { "", P::Exec }, { "Entity", P::Entity } },
				Emit::Special, "spawnprefab");
			set(GraphNodeType::DestroyEntity, "Destroy", "Entity",
				{ { "", P::Exec }, { "Entity", P::Entity } }, { { "", P::Exec } },
				Emit::Statement, "{1}.Destroy();");
			set(GraphNodeType::GetParent, "Get Parent", "Entity",
				{ { "Entity", P::Entity } }, { { "", P::Entity } },
				Emit::Expression, "{0}.Parent");
			set(GraphNodeType::EntityExists, "Exists", "Entity",
				{ { "Entity", P::Entity } }, { { "", P::Bool } },
				Emit::Expression, "{0}.Exists");
			set(GraphNodeType::GetEntityName, "Get Name", "Entity",
				{ { "Entity", P::Entity } }, { { "", P::String } },
				Emit::Expression, "{0}.Name");

			// --- transform
			set(GraphNodeType::GetPosition, "Get Position", "Transform",
				{ { "Entity", P::Entity } }, { { "", P::Vec3 } },
				Emit::Expression, "{0}.Position");
			set(GraphNodeType::SetPosition, "Set Position", "Transform",
				{ { "", P::Exec }, { "Entity", P::Entity }, { "Value", P::Vec3 } }, { { "", P::Exec } },
				Emit::Statement, "{1}.Position = {2};");
			set(GraphNodeType::GetRotation, "Get Rotation", "Transform",
				{ { "Entity", P::Entity } }, { { "", P::Vec3 } },
				Emit::Expression, "{0}.Rotation");
			set(GraphNodeType::SetRotation, "Set Rotation", "Transform",
				{ { "", P::Exec }, { "Entity", P::Entity }, { "Value", P::Vec3 } }, { { "", P::Exec } },
				Emit::Statement, "{1}.Rotation = {2};");
			set(GraphNodeType::GetScale, "Get Scale", "Transform",
				{ { "Entity", P::Entity } }, { { "", P::Vec3 } },
				Emit::Expression, "{0}.Scale");
			set(GraphNodeType::SetScale, "Set Scale", "Transform",
				{ { "", P::Exec }, { "Entity", P::Entity }, { "Value", P::Vec3 } }, { { "", P::Exec } },
				Emit::Statement, "{1}.Scale = {2};");
			set(GraphNodeType::TranslateBy, "Translate", "Transform",
				{ { "", P::Exec }, { "Entity", P::Entity }, { "Delta", P::Vec3 } }, { { "", P::Exec } },
				Emit::Statement, "{1}.Translate({2});");
			set(GraphNodeType::RotateBy, "Rotate", "Transform",
				{ { "", P::Exec }, { "Entity", P::Entity }, { "Delta", P::Vec3 } }, { { "", P::Exec } },
				Emit::Statement, "{1}.Rotate({2});");
			set(GraphNodeType::LookAtPoint, "Look At", "Transform",
				{ { "", P::Exec }, { "Entity", P::Entity }, { "Target", P::Vec3 } }, { { "", P::Exec } },
				Emit::Statement, "{1}.LookAt({2});");
			set(GraphNodeType::GetWorldPosition, "World Position", "Transform",
				{ { "Entity", P::Entity } }, { { "", P::Vec3 } },
				Emit::Expression, "{0}.WorldPosition");
			set(GraphNodeType::GetForward, "Forward", "Transform",
				{ { "Entity", P::Entity } }, { { "", P::Vec3 } },
				Emit::Expression, "{0}.Forward");
			set(GraphNodeType::GetRight, "Right", "Transform",
				{ { "Entity", P::Entity } }, { { "", P::Vec3 } },
				Emit::Expression, "{0}.Right");
			set(GraphNodeType::GetUp, "Up", "Transform",
				{ { "Entity", P::Entity } }, { { "", P::Vec3 } },
				Emit::Expression, "{0}.Up");

			// --- physics
			set(GraphNodeType::AddForce, "Add Force", "Physics",
				{ { "", P::Exec }, { "Entity", P::Entity }, { "Force", P::Vec3 } }, { { "", P::Exec } },
				Emit::Statement, "{1}.AddForce({2});");
			set(GraphNodeType::AddImpulse, "Add Impulse", "Physics",
				{ { "", P::Exec }, { "Entity", P::Entity }, { "Impulse", P::Vec3 } }, { { "", P::Exec } },
				Emit::Statement, "{1}.AddImpulse({2});");
			set(GraphNodeType::GetVelocity, "Get Velocity", "Physics",
				{ { "Entity", P::Entity } }, { { "", P::Vec3 } },
				Emit::Expression, "{0}.LinearVelocity");
			set(GraphNodeType::SetVelocity, "Set Velocity", "Physics",
				{ { "", P::Exec }, { "Entity", P::Entity }, { "Value", P::Vec3 } }, { { "", P::Exec } },
				Emit::Statement, "{1}.LinearVelocity = {2};");
			set(GraphNodeType::RaycastHit, "Raycast Hit", "Physics",
				{ { "Origin", P::Vec3 }, { "Direction", P::Vec3 } }, { { "", P::Bool } },
				Emit::Expression, "Entity.Raycast({0}, {1}).Hit");
			set(GraphNodeType::RaycastEntity, "Raycast Entity", "Physics",
				{ { "Origin", P::Vec3 }, { "Direction", P::Vec3 } }, { { "", P::Entity } },
				Emit::Expression, "Entity.Raycast({0}, {1}).Entity");
			set(GraphNodeType::RaycastPoint, "Raycast Point", "Physics",
				{ { "Origin", P::Vec3 }, { "Direction", P::Vec3 } }, { { "", P::Vec3 } },
				Emit::Expression, "Entity.Raycast({0}, {1}).Position");
			set(GraphNodeType::RaycastDistance, "Raycast Distance", "Physics",
				{ { "Origin", P::Vec3 }, { "Direction", P::Vec3 } }, { { "", P::Float } },
				Emit::Expression, "Entity.Raycast({0}, {1}).Distance");

			// --- input
			set(GraphNodeType::ActionDown, "Action Down", "Input",
				{ { "Action", P::String } }, { { "", P::Bool } },
				Emit::Expression, "Input.IsActionDown({0})");
			set(GraphNodeType::ActionPressed, "Action Pressed", "Input",
				{ { "Action", P::String } }, { { "", P::Bool } },
				Emit::Expression, "Input.WasActionPressed({0})");
			set(GraphNodeType::ActionReleased, "Action Released", "Input",
				{ { "Action", P::String } }, { { "", P::Bool } },
				Emit::Expression, "Input.WasActionReleased({0})");
			set(GraphNodeType::InputAxis, "Axis", "Input",
				{ { "Axis", P::String } }, { { "", P::Float } },
				Emit::Expression, "Input.GetAxis({0})");

			// --- time
			set(GraphNodeType::FixedDelta, "Fixed Delta", "Time",
				{}, { { "", P::Float } },
				Emit::Expression, "Time.FixedDeltaTime");
			set(GraphNodeType::ElapsedTime, "Elapsed", "Time",
				{}, { { "", P::Float } },
				Emit::Expression, "Time.Elapsed");

			// --- audio
			set(GraphNodeType::PlaySound2D, "Play Sound", "Audio",
				{ { "", P::Exec }, { "Clip", P::String } }, { { "", P::Exec } },
				Emit::Statement, "Audio.PlayOneShot2D({1});");
			set(GraphNodeType::PlaySoundAt, "Play Sound At", "Audio",
				{ { "", P::Exec }, { "Clip", P::String }, { "Position", P::Vec3 } }, { { "", P::Exec } },
				Emit::Statement, "Audio.PlayOneShotAt({1}, {2});");
			set(GraphNodeType::PlaySource, "Play Source", "Audio",
				{ { "", P::Exec }, { "Entity", P::Entity } }, { { "", P::Exec } },
				Emit::Statement, "{1}.PlaySource();");
			set(GraphNodeType::StopSource, "Stop Source", "Audio",
				{ { "", P::Exec }, { "Entity", P::Entity } }, { { "", P::Exec } },
				Emit::Statement, "{1}.StopSource();");

			// --- component
			set(GraphNodeType::HasComponent, "Has Component", "Component",
				{ { "Entity", P::Entity }, { "Name", P::String } }, { { "", P::Bool } },
				Emit::Expression, "{0}.HasComponent({1})");
			set(GraphNodeType::AddComponent, "Add Component", "Component",
				{ { "", P::Exec }, { "Entity", P::Entity }, { "Name", P::String } }, { { "", P::Exec } },
				Emit::Statement, "{1}.AddComponent({2});");
			set(GraphNodeType::RemoveComponent, "Remove Component", "Component",
				{ { "", P::Exec }, { "Entity", P::Entity }, { "Name", P::String } }, { { "", P::Exec } },
				Emit::Statement, "{1}.RemoveComponent({2});");
			set(GraphNodeType::GetField, "Get Field", "Component",
				{}, { { "Value", P::String } },
				Emit::Special, "getfield");
			set(GraphNodeType::SetField, "Set Field", "Component",
				{ { "", P::Exec }, { "Value", P::String } }, { { "", P::Exec } },
				Emit::Special, "setfield");

			// --- ui
			set(GraphNodeType::SetUIText, "Set UI Text", "UI",
				{ { "", P::Exec }, { "Entity", P::Entity }, { "Text", P::String } }, { { "", P::Exec } },
				Emit::Statement, "{1}.Text = {2};");
			set(GraphNodeType::ButtonClicked, "Button Clicked", "UI",
				{ { "Entity", P::Entity } }, { { "", P::Bool } },
				Emit::Expression, "{0}.WasButtonClicked()");

			// --- output
			set(GraphNodeType::Log, "Log", "Output",
				{ { "", P::Exec }, { "Message", P::String } }, { { "", P::Exec } },
				Emit::Special, "log");
			set(GraphNodeType::LogWarning, "Log Warning", "Output",
				{ { "", P::Exec }, { "Message", P::String } }, { { "", P::Exec } },
				Emit::Special, "logwarn");
			return descs;
		}

		struct NodeName { GraphNodeType Type; const char* Name; };

		// The name written to the file. Deliberately *not* the display name:
		// renaming a node in the menu must not orphan every saved graph.
		const NodeName kNodeNames[] = {
			{ GraphNodeType::None, "None" },
			{ GraphNodeType::OnCreate, "OnCreate" },
			{ GraphNodeType::OnTick, "OnTick" },
			{ GraphNodeType::OnFrame, "OnFrame" },
			{ GraphNodeType::OnDestroy, "OnDestroy" },
			{ GraphNodeType::OnCollisionEnter, "OnCollisionEnter" },
			{ GraphNodeType::OnCollisionStay, "OnCollisionStay" },
			{ GraphNodeType::OnCollisionExit, "OnCollisionExit" },
			{ GraphNodeType::OnTriggerEnter, "OnTriggerEnter" },
			{ GraphNodeType::OnTriggerStay, "OnTriggerStay" },
			{ GraphNodeType::OnTriggerExit, "OnTriggerExit" },
			{ GraphNodeType::Branch, "Branch" },
			{ GraphNodeType::Sequence, "Sequence" },
			{ GraphNodeType::ForLoop, "ForLoop" },
			{ GraphNodeType::WhileLoop, "WhileLoop" },
			{ GraphNodeType::BreakLoop, "BreakLoop" },
			{ GraphNodeType::FunctionEntry, "FunctionEntry" },
			{ GraphNodeType::CallFunction, "CallFunction" },
			{ GraphNodeType::LiteralBool, "LiteralBool" },
			{ GraphNodeType::LiteralFloat, "LiteralFloat" },
			{ GraphNodeType::LiteralVec3, "LiteralVec3" },
			{ GraphNodeType::LiteralString, "LiteralString" },
			{ GraphNodeType::SelfEntity, "SelfEntity" },
			{ GraphNodeType::GetNumber, "GetNumber" },
			{ GraphNodeType::SetNumber, "SetNumber" },
			{ GraphNodeType::GetVector, "GetVector" },
			{ GraphNodeType::SetVector, "SetVector" },
			{ GraphNodeType::GetFlag, "GetFlag" },
			{ GraphNodeType::SetFlag, "SetFlag" },
			{ GraphNodeType::GetText, "GetText" },
			{ GraphNodeType::SetText, "SetText" },
			{ GraphNodeType::GetEntityVar, "GetEntityVar" },
			{ GraphNodeType::SetEntityVar, "SetEntityVar" },
			{ GraphNodeType::GetNumbers, "GetNumbers" },
			{ GraphNodeType::SetNumbers, "SetNumbers" },
			{ GraphNodeType::GetEntities, "GetEntities" },
			{ GraphNodeType::SetEntities, "SetEntities" },
			{ GraphNodeType::GetNumberMap, "GetNumberMap" },
			{ GraphNodeType::SetNumberMap, "SetNumberMap" },
			{ GraphNodeType::GetEntityMap, "GetEntityMap" },
			{ GraphNodeType::SetEntityMap, "SetEntityMap" },
			{ GraphNodeType::NumbersAdd, "NumbersAdd" },
			{ GraphNodeType::NumbersAt, "NumbersAt" },
			{ GraphNodeType::NumbersCount, "NumbersCount" },
			{ GraphNodeType::NumbersHas, "NumbersHas" },
			{ GraphNodeType::NumbersRemoveAt, "NumbersRemoveAt" },
			{ GraphNodeType::NumbersClear, "NumbersClear" },
			{ GraphNodeType::ForEachNumber, "ForEachNumber" },
			{ GraphNodeType::EntitiesAdd, "EntitiesAdd" },
			{ GraphNodeType::EntitiesAt, "EntitiesAt" },
			{ GraphNodeType::EntitiesCount, "EntitiesCount" },
			{ GraphNodeType::EntitiesRemoveAt, "EntitiesRemoveAt" },
			{ GraphNodeType::EntitiesClear, "EntitiesClear" },
			{ GraphNodeType::ForEachEntity, "ForEachEntity" },
			{ GraphNodeType::NumberMapSet, "NumberMapSet" },
			{ GraphNodeType::NumberMapGet, "NumberMapGet" },
			{ GraphNodeType::NumberMapHas, "NumberMapHas" },
			{ GraphNodeType::NumberMapRemove, "NumberMapRemove" },
			{ GraphNodeType::NumberMapCount, "NumberMapCount" },
			{ GraphNodeType::NumberMapClear, "NumberMapClear" },
			{ GraphNodeType::EntityMapSet, "EntityMapSet" },
			{ GraphNodeType::EntityMapGet, "EntityMapGet" },
			{ GraphNodeType::EntityMapHas, "EntityMapHas" },
			{ GraphNodeType::EntityMapRemove, "EntityMapRemove" },
			{ GraphNodeType::EntityMapCount, "EntityMapCount" },
			{ GraphNodeType::EntityMapClear, "EntityMapClear" },
			{ GraphNodeType::Add, "Add" },
			{ GraphNodeType::Subtract, "Subtract" },
			{ GraphNodeType::Multiply, "Multiply" },
			{ GraphNodeType::Divide, "Divide" },
			{ GraphNodeType::Compare, "Compare" },
			{ GraphNodeType::MinOf, "MinOf" },
			{ GraphNodeType::MaxOf, "MaxOf" },
			{ GraphNodeType::AbsOf, "AbsOf" },
			{ GraphNodeType::ClampOf, "ClampOf" },
			{ GraphNodeType::LerpOf, "LerpOf" },
			{ GraphNodeType::SinOf, "SinOf" },
			{ GraphNodeType::CosOf, "CosOf" },
			{ GraphNodeType::AndOf, "AndOf" },
			{ GraphNodeType::OrOf, "OrOf" },
			{ GraphNodeType::NotOf, "NotOf" },
			{ GraphNodeType::MakeVector, "MakeVector" },
			{ GraphNodeType::BreakVectorX, "BreakVectorX" },
			{ GraphNodeType::BreakVectorY, "BreakVectorY" },
			{ GraphNodeType::BreakVectorZ, "BreakVectorZ" },
			{ GraphNodeType::VectorAdd, "VectorAdd" },
			{ GraphNodeType::VectorSubtract, "VectorSubtract" },
			{ GraphNodeType::VectorScale, "VectorScale" },
			{ GraphNodeType::VectorLength, "VectorLength" },
			{ GraphNodeType::VectorNormalize, "VectorNormalize" },
			{ GraphNodeType::VectorDot, "VectorDot" },
			{ GraphNodeType::FindByName, "FindByName" },
			{ GraphNodeType::SpawnEntity, "SpawnEntity" },
			{ GraphNodeType::SpawnPrefab, "SpawnPrefab" },
			{ GraphNodeType::DestroyEntity, "DestroyEntity" },
			{ GraphNodeType::GetParent, "GetParent" },
			{ GraphNodeType::EntityExists, "EntityExists" },
			{ GraphNodeType::GetEntityName, "GetEntityName" },
			{ GraphNodeType::GetPosition, "GetPosition" },
			{ GraphNodeType::SetPosition, "SetPosition" },
			{ GraphNodeType::GetRotation, "GetRotation" },
			{ GraphNodeType::SetRotation, "SetRotation" },
			{ GraphNodeType::GetScale, "GetScale" },
			{ GraphNodeType::SetScale, "SetScale" },
			{ GraphNodeType::TranslateBy, "TranslateBy" },
			{ GraphNodeType::RotateBy, "RotateBy" },
			{ GraphNodeType::LookAtPoint, "LookAtPoint" },
			{ GraphNodeType::GetWorldPosition, "GetWorldPosition" },
			{ GraphNodeType::GetForward, "GetForward" },
			{ GraphNodeType::GetRight, "GetRight" },
			{ GraphNodeType::GetUp, "GetUp" },
			{ GraphNodeType::AddForce, "AddForce" },
			{ GraphNodeType::AddImpulse, "AddImpulse" },
			{ GraphNodeType::GetVelocity, "GetVelocity" },
			{ GraphNodeType::SetVelocity, "SetVelocity" },
			{ GraphNodeType::RaycastHit, "RaycastHit" },
			{ GraphNodeType::RaycastEntity, "RaycastEntity" },
			{ GraphNodeType::RaycastPoint, "RaycastPoint" },
			{ GraphNodeType::RaycastDistance, "RaycastDistance" },
			{ GraphNodeType::ActionDown, "ActionDown" },
			{ GraphNodeType::ActionPressed, "ActionPressed" },
			{ GraphNodeType::ActionReleased, "ActionReleased" },
			{ GraphNodeType::InputAxis, "InputAxis" },
			{ GraphNodeType::FixedDelta, "FixedDelta" },
			{ GraphNodeType::ElapsedTime, "ElapsedTime" },
			{ GraphNodeType::PlaySound2D, "PlaySound2D" },
			{ GraphNodeType::PlaySoundAt, "PlaySoundAt" },
			{ GraphNodeType::PlaySource, "PlaySource" },
			{ GraphNodeType::StopSource, "StopSource" },
			{ GraphNodeType::HasComponent, "HasComponent" },
			{ GraphNodeType::AddComponent, "AddComponent" },
			{ GraphNodeType::RemoveComponent, "RemoveComponent" },
			{ GraphNodeType::GetField, "GetField" },
			{ GraphNodeType::SetField, "SetField" },
			{ GraphNodeType::SetUIText, "SetUIText" },
			{ GraphNodeType::ButtonClicked, "ButtonClicked" },
			{ GraphNodeType::Log, "Log" },
			{ GraphNodeType::LogWarning, "LogWarning" },
		};
	}

	const char* GraphPinTypeName(GraphPinType type)
	{
		switch (type)
		{
			case GraphPinType::Exec:   return "Exec";
			case GraphPinType::Bool:   return "Bool";
			case GraphPinType::Float:  return "Float";
			case GraphPinType::Vec3:   return "Vec3";
			case GraphPinType::String: return "String";
			case GraphPinType::Entity: return "Entity";
			case GraphPinType::NumberList: return "Numbers";
			case GraphPinType::EntityList: return "Entities";
			case GraphPinType::NumberMap:  return "Number Map";
			case GraphPinType::EntityMap:  return "Entity Map";
		}
		return "Exec";
	}

	const std::vector<GraphNodeDesc>& GraphNodeDescs()
	{
		static const std::vector<GraphNodeDesc> descs = BuildDescs();
		return descs;
	}

	const GraphNodeDesc& GraphNodeDescOf(GraphNodeType type)
	{
		const std::vector<GraphNodeDesc>& descs = GraphNodeDescs();
		const size_t index = (size_t)type;
		return index < descs.size() ? descs[index] : descs[0];
	}

	const char* GraphNodeTypeName(GraphNodeType type)
	{
		for (const NodeName& entry : kNodeNames)
		{
			if (entry.Type == type)
				return entry.Name;
		}
		return "None";
	}

	GraphNodeType GraphNodeTypeFromName(const std::string& name)
	{
		for (const NodeName& entry : kNodeNames)
		{
			if (name == entry.Name)
				return entry.Type;
		}
		return GraphNodeType::None;
	}

	ScriptGraph ScriptGraph::Starter(const std::string& className)
	{
		ScriptGraph graph;
		const uint32_t event = graph.AddNode(GraphNodeType::OnCreate,
											 Vec2(-260.0f, -40.0f));
		const uint32_t message = graph.AddNode(GraphNodeType::Log,
											   Vec2(60.0f, -40.0f));
		const uint32_t text = graph.AddNode(GraphNodeType::LiteralString,
											Vec2(-260.0f, 120.0f));

		if (GraphNode* node = graph.FindNode(text))
			node->Text = className + " ready";

		graph.AddLink(event, 0, message, 0);
		graph.AddLink(text, 0, message, 1);
		return graph;
	}

	bool ScriptGraph::PinAccepts(GraphPinType from, GraphPinType to)
	{
		if (from == to)
			return true;

		// Everything has a text form, and text is what the engine's named
		// field API speaks. Exec is not a value and converts to nothing.
		return to == GraphPinType::String
			&& (from == GraphPinType::Bool || from == GraphPinType::Float
				|| from == GraphPinType::Vec3);
	}

	GraphNode* ScriptGraph::FindNode(uint32_t id)
	{
		for (GraphNode& node : m_Nodes)
		{
			if (node.Id == id)
				return &node;
		}
		return nullptr;
	}

	const GraphNode* ScriptGraph::FindNode(uint32_t id) const
	{
		return const_cast<ScriptGraph*>(this)->FindNode(id);
	}

	uint32_t ScriptGraph::AddNode(GraphNodeType type, const Vec2& position)
	{
		GraphNode node;
		node.Id = m_NextNodeId++;
		node.Type = type;
		node.Position = position;
		m_Nodes.push_back(node);
		return node.Id;
	}

	void ScriptGraph::RemoveNode(uint32_t id)
	{
		m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
									 [id](const GraphLink& link)
									 {
										 return link.FromNode == id || link.ToNode == id;
									 }),
					  m_Links.end());

		m_Nodes.erase(std::remove_if(m_Nodes.begin(), m_Nodes.end(),
									 [id](const GraphNode& node) { return node.Id == id; }),
					  m_Nodes.end());
	}

	bool ScriptGraph::CanLink(uint32_t fromNode, uint32_t fromPin,
							  uint32_t toNode, uint32_t toPin, std::string& reason) const
	{
		if (fromNode == toNode)
		{
			reason = "a node cannot link to itself";
			return false;
		}

		const GraphNode* from = FindNode(fromNode);
		const GraphNode* to = FindNode(toNode);
		if (!from || !to)
		{
			reason = "one of those nodes is gone";
			return false;
		}

		const GraphNodeDesc& fromDesc = GraphNodeDescOf(from->Type);
		const GraphNodeDesc& toDesc = GraphNodeDescOf(to->Type);
		if (fromPin >= fromDesc.Outputs.size() || toPin >= toDesc.Inputs.size())
		{
			reason = "that pin does not exist";
			return false;
		}

		const GraphPinType fromType = fromDesc.Outputs[fromPin].Type;
		const GraphPinType toType = toDesc.Inputs[toPin].Type;
		if (!PinAccepts(fromType, toType))
		{
			reason = std::string("a ") + GraphPinTypeName(fromType) + " pin does not fit a "
				   + GraphPinTypeName(toType) + " one";

			// The one case worth explaining rather than just refusing, because
			// the reverse *is* allowed and the asymmetry looks arbitrary until
			// somebody says why.
			if (fromType == GraphPinType::String && toType != GraphPinType::Exec)
				reason += " -- text does not convert back, because a parse can fail";
			return false;
		}

		return true;
	}

	uint32_t ScriptGraph::AddLink(uint32_t fromNode, uint32_t fromPin,
								  uint32_t toNode, uint32_t toPin)
	{
		const GraphNode* from = FindNode(fromNode);
		const bool exec = from
			&& fromPin < GraphNodeDescOf(from->Type).Outputs.size()
			&& GraphNodeDescOf(from->Type).Outputs[fromPin].Type == GraphPinType::Exec;

		// An input takes one link, and an *exec output* takes one too: two
		// would be an order of execution nobody wrote down. A data output may
		// feed as many inputs as it likes -- that is a value being read twice,
		// which is ordinary.
		m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
									 [&](const GraphLink& link)
									 {
										 if (link.ToNode == toNode && link.ToPin == toPin)
											 return true;
										 return exec && link.FromNode == fromNode
											 && link.FromPin == fromPin;
									 }),
					  m_Links.end());

		GraphLink link;
		link.Id = m_NextLinkId++;
		link.FromNode = fromNode;
		link.FromPin = fromPin;
		link.ToNode = toNode;
		link.ToPin = toPin;
		m_Links.push_back(link);
		return link.Id;
	}

	void ScriptGraph::RemoveLink(uint32_t id)
	{
		m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
									 [id](const GraphLink& link) { return link.Id == id; }),
					  m_Links.end());
	}

	bool ScriptGraph::IsInputLinked(uint32_t node, uint32_t pin) const
	{
		for (const GraphLink& link : m_Links)
		{
			if (link.ToNode == node && link.ToPin == pin)
				return true;
		}
		return false;
	}

	std::vector<GraphIssue> ScriptGraph::Validate() const
	{
		std::vector<GraphIssue> issues;

		auto error = [&issues](uint32_t node, std::string message)
		{
			issues.push_back({ GraphIssueSeverity::Error, node, std::move(message) });
		};
		auto warn = [&issues](uint32_t node, std::string message)
		{
			issues.push_back({ GraphIssueSeverity::Warning, node, std::move(message) });
		};

		// --- a link whose ends no longer agree about their type ---------------
		//
		// The canvas refuses to *create* one, so this catches the two ways one
		// arrives anyway: a hand-edited file, and a node whose pins changed
		// between builds. Left unchecked it is the worst kind of defect here --
		// the graph looks right and generates something that will not compile.
		for (const GraphLink& link : m_Links)
		{
			const GraphNode* from = FindNode(link.FromNode);
			const GraphNode* to = FindNode(link.ToNode);
			if (!from || !to)
				continue;

			const GraphNodeDesc& fromDesc = GraphNodeDescOf(from->Type);
			const GraphNodeDesc& toDesc = GraphNodeDescOf(to->Type);

			if (link.FromPin >= fromDesc.Outputs.size()
				|| link.ToPin >= toDesc.Inputs.size())
			{
				error(link.ToNode, std::string(toDesc.Name)
					  + " is linked through a pin that no longer exists");
				continue;
			}

			const GraphPinType fromType = fromDesc.Outputs[link.FromPin].Type;
			const GraphPinType toType = toDesc.Inputs[link.ToPin].Type;
			if (!PinAccepts(fromType, toType))
			{
				error(link.ToNode, std::string(toDesc.Name) + " takes a "
					  + GraphPinTypeName(toType) + " where "
					  + fromDesc.Name + " gives a " + GraphPinTypeName(fromType));
			}
		}

		// --- inputs nothing feeds ---------------------------------------------
		size_t events = 0;
		for (const GraphNode& node : m_Nodes)
		{
			const GraphNodeDesc& desc = GraphNodeDescOf(node.Type);
			if (desc.IsEvent)
				events++;

			for (uint32_t pin = 0; pin < desc.Inputs.size(); pin++)
			{
				if (IsInputLinked(node.Id, pin))
					continue;

				// An unconnected *exec* input is only a problem if nothing can
				// reach the node, which the reachability pass below answers
				// properly. An unconnected *data* input has no value at all --
				// unless the node carries its own literal for it, which Log
				// does. Getting this wrong is not cosmetic: a false error
				// blocks the file being written, so the check that guards
				// against nothing being generated would itself be the reason.
				if (desc.Inputs[pin].Type == GraphPinType::Exec)
					continue;
				if ((node.Type == GraphNodeType::Log
					 || node.Type == GraphNodeType::LogWarning)
					&& pin == 1 && !node.Text.empty())
					continue;

				const char* name = desc.Inputs[pin].Name;
				error(node.Id, std::string(desc.Name) + "'s "
					  + (name && name[0] ? name : "input") + " has nothing feeding it");
			}

			// A field access with no field is a node that cannot be written.
			if (node.Type == GraphNodeType::GetField || node.Type == GraphNodeType::SetField)
			{
				const size_t dot = node.Text.find('.');
				if (node.Text.empty())
					error(node.Id, std::string(desc.Name) + " names no field");
				else if (dot == std::string::npos || dot == 0 || dot + 1 >= node.Text.size())
					error(node.Id, std::string(desc.Name) + " wants Component.Field, not '"
						  + node.Text + "'");
			}

		}

		if (m_Nodes.empty())
			return issues;

		if (events == 0)
			error(0, "no event node, so this graph generates nothing");

		// --- one event of each kind -------------------------------------------
		for (size_t i = 0; i < m_Nodes.size(); i++)
		{
			if (!GraphNodeDescOf(m_Nodes[i].Type).IsEvent)
				continue;
			for (size_t j = i + 1; j < m_Nodes.size(); j++)
			{
				if (m_Nodes[j].Type != m_Nodes[i].Type)
					continue;
				// Two would be one method written twice, and which one wins
				// would be an accident of ordering.
				error(m_Nodes[j].Id, std::string(GraphNodeDescOf(m_Nodes[j].Type).Name)
					  + " appears twice; there can be one of each event");
			}
		}

		// --- reachability and cycles, over the exec chain ----------------------
		//
		// Walked from the events, because that is exactly how the generator
		// will walk it: anything it does not reach is not emitted, and a loop
		// is a method that never returns.
		std::vector<uint32_t> reached;
		std::vector<uint32_t> stack;

		auto contains = [](const std::vector<uint32_t>& list, uint32_t id)
		{
			return std::find(list.begin(), list.end(), id) != list.end();
		};

		std::function<void(uint32_t)> walk = [&](uint32_t id)
		{
			if (contains(stack, id))
			{
				const GraphNode* node = FindNode(id);
				error(id, std::string(node ? GraphNodeDescOf(node->Type).Name : "a node")
					  + " is part of a loop, which would never finish");
				return;
			}
			if (contains(reached, id))
				return;

			reached.push_back(id);
			stack.push_back(id);

			const GraphNode* node = FindNode(id);
			if (node)
			{
				const GraphNodeDesc& desc = GraphNodeDescOf(node->Type);
				for (uint32_t pin = 0; pin < desc.Outputs.size(); pin++)
				{
					if (desc.Outputs[pin].Type != GraphPinType::Exec)
						continue;
					for (const GraphLink& link : m_Links)
					{
						if (link.FromNode == id && link.FromPin == pin)
							walk(link.ToNode);
					}
				}
			}

			stack.pop_back();
		};

		for (const GraphNode& node : m_Nodes)
		{
			// A function entry is a root exactly as an event is. Without this
			// every function body reports "not connected to any event", which
			// is true and useless.
			if (GraphNodeDescOf(node.Type).IsEvent
				|| node.Type == GraphNodeType::FunctionEntry)
				walk(node.Id);
		}

		// A pure-data node is reached by being *read*, not by exec, so it
		// counts as reachable when something reachable links to it.
		bool grew = true;
		while (grew)
		{
			grew = false;
			for (const GraphLink& link : m_Links)
			{
				if (contains(reached, link.ToNode) && !contains(reached, link.FromNode))
				{
					reached.push_back(link.FromNode);
					grew = true;
				}
			}
		}

		for (const GraphNode& node : m_Nodes)
		{
			if (!contains(reached, node.Id))
				warn(node.Id, std::string(GraphNodeDescOf(node.Type).Name)
					 + " is not connected to any event, so it will not run");
		}

		// --- functions -------------------------------------------------------
		std::vector<std::string> declared;
		for (const GraphNode& node : m_Nodes)
		{
			if (node.Type != GraphNodeType::FunctionEntry)
				continue;
			if (node.Text.empty())
			{
				error(node.Id, "this function has no name");
				continue;
			}
			if (std::find(declared.begin(), declared.end(), node.Text) != declared.end())
				error(node.Id, "there is already a function called '" + node.Text + "'");
			else
				declared.push_back(node.Text);
		}

		for (const GraphNode& node : m_Nodes)
		{
			if (node.Type != GraphNodeType::CallFunction)
				continue;
			if (node.Text.empty())
				error(node.Id, "this call names no function");
			else if (std::find(declared.begin(), declared.end(), node.Text) == declared.end())
				error(node.Id, "there is no function called '" + node.Text + "'");
		}

		// --- scope: what a loop's Body reaches ---------------------------------
		//
		// The one thing a graph is otherwise missing. A loop's Index is only
		// meaningful inside its Body and a Break only inside a loop, and both
		// are answerable from the exec chain: the scope *is* what Body reaches.
		// Without this, reading Index after the loop generates a reference to a
		// counter that has gone out of scope, and the error lands on generated
		// code rather than on the node that asked for it.
		auto bodyOf = [this](const GraphNode& loop, uint32_t pin)
		{
			std::vector<uint32_t> inside;
			std::vector<uint32_t> pending;

			for (const GraphLink& link : m_Links)
			{
				if (link.FromNode == loop.Id && link.FromPin == pin)
					pending.push_back(link.ToNode);
			}

			while (!pending.empty())
			{
				const uint32_t id = pending.back();
				pending.pop_back();
				if (std::find(inside.begin(), inside.end(), id) != inside.end())
					continue;
				inside.push_back(id);

				const GraphNode* node = FindNode(id);
				if (!node)
					continue;

				const GraphNodeDesc& desc = GraphNodeDescOf(node->Type);
				for (uint32_t out = 0; out < desc.Outputs.size(); out++)
				{
					if (desc.Outputs[out].Type != GraphPinType::Exec)
						continue;
					for (const GraphLink& link : m_Links)
					{
						if (link.FromNode == id && link.FromPin == out)
							pending.push_back(link.ToNode);
					}
				}

				// Data feeding a statement in the body is in the body too, so
				// an expression built inside the loop counts as inside it.
				for (const GraphLink& link : m_Links)
				{
					if (link.ToNode == id)
						pending.push_back(link.FromNode);
				}
			}
			return inside;
		};

		// Every loop, and every value it offers: For Loop's Index, and For
		// Each's Element and Index. Written over the whole family rather than
		// per node type, because the next loop added would otherwise be the
		// one nobody remembers to add here.
		for (const GraphNode& node : m_Nodes)
		{
			const bool loop = node.Type == GraphNodeType::ForLoop
						   || node.Type == GraphNodeType::ForEachNumber
						   || node.Type == GraphNodeType::ForEachEntity;
			if (!loop)
				continue;

			const GraphNodeDesc& desc = GraphNodeDescOf(node.Type);
			const std::vector<uint32_t> inside = bodyOf(node, 0);

			for (uint32_t pin = 0; pin < desc.Outputs.size(); pin++)
			{
				if (desc.Outputs[pin].Type == GraphPinType::Exec)
					continue;
				for (const GraphLink& link : m_Links)
				{
					if (link.FromNode != node.Id || link.FromPin != pin)
						continue;
					if (std::find(inside.begin(), inside.end(), link.ToNode) != inside.end())
						continue;
					error(link.ToNode, std::string("this reads ") + desc.Name + "'s "
						  + desc.Outputs[pin].Name + " from outside its Body, where "
						  "that value does not exist");
				}
			}
		}

		for (const GraphNode& node : m_Nodes)
		{
			if (node.Type != GraphNodeType::BreakLoop)
				continue;

			bool enclosed = false;
			for (const GraphNode& loop : m_Nodes)
			{
				if (loop.Type != GraphNodeType::ForLoop
					&& loop.Type != GraphNodeType::WhileLoop
					&& loop.Type != GraphNodeType::ForEachNumber
					&& loop.Type != GraphNodeType::ForEachEntity)
					continue;
				const std::vector<uint32_t> inside = bodyOf(loop, 0);
				if (std::find(inside.begin(), inside.end(), node.Id) != inside.end())
				{
					enclosed = true;
					break;
				}
			}
			if (!enclosed)
				error(node.Id, "Break is not inside a loop, so there is nothing to break out of");
		}

		// Errors first: a panel showing five warnings above the one error that
		// stops the file being written has buried the thing that matters.
		std::stable_sort(issues.begin(), issues.end(),
						 [](const GraphIssue& a, const GraphIssue& b)
						 {
							 return a.Severity > b.Severity;
						 });
		return issues;
	}

	void ScriptGraph::SetNextIds(uint32_t node, uint32_t link)
	{
		m_NextNodeId = Math::Max(node, 1u);
		m_NextLinkId = Math::Max(link, 1u);
	}

	void ScriptGraph::Clear()
	{
		m_Nodes.clear();
		m_Links.clear();
		m_NextNodeId = 1;
		m_NextLinkId = 1;
	}

	void ScriptGraph::SetContents(std::vector<GraphNode> nodes, std::vector<GraphLink> links)
	{
		m_Nodes = std::move(nodes);
		m_Links = std::move(links);
	}
}
