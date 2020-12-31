#include "stdafx.h"

#include "SpehsEngine/Audio/AudioLib.h"
#include "SpehsEngine/Core/CoreLib.h"
#include "SpehsEngine/Core/DeltaTimeSystem.h"
#include "SpehsEngine/Core/StringOperations.h"
#include "SpehsEngine/Core/Inifile.h"
#include "SpehsEngine/Core/Console.h"
#include "SpehsEngine/Core/Thread.h"
#include "SpehsEngine/Core/TypelessPointer.h"
#include "SpehsEngine/Math/MathLib.h"
#include "SpehsEngine/Net/NetLib.h"
#include "SpehsEngine/Net/ConnectionManager.h"
#include "SpehsEngine/Net/IOService.h"
#include "SpehsEngine/Net/AddressUtilityFunctions.h"
#include "SpehsEngine/Rendering/RenderingLib.h"
#include "SpehsEngine/Rendering/Camera2D.h"
#include "SpehsEngine/Rendering/ConsoleVisualizer.h"
#include "SpehsEngine/Rendering/BatchManager.h"
#include "SpehsEngine/Rendering/ShaderManager.h"
#include "SpehsEngine/Rendering/TextureManager.h"
#include "SpehsEngine/Rendering/Text.h"
#include "SpehsEngine/Rendering/Window.h"
#include "SpehsEngine/Input/InputLib.h"
#include "SpehsEngine/Input/InputManager.h"
#include "SpehsEngine/Input/EventCatcher.h"
#include "SpehsEngine/Input/EventSignaler.h"
#include "SpehsEngine/Physics/PhysicsLib.h"
#include "SpehsEngine/GUI/GUILib.h"
#include "SpehsEngine/GUI/GUIRectangle.h"
#include "SpehsEngine/Debug/DebugLib.h"
#include "SpehsEngine/Debug/ConnectionProfiler.h"
#include "SpehsEngine/Debug/ScopeProfilerVisualizer.h"
#include <thread>


int main()
{
	se::CoreLib core;
	se::NetLib net(core);
	se::MathLib math(core);
	se::PhysicsLib physics(math);
	se::AudioLib audio(core);
	se::rendering::RenderingLib rendering(math);
	se::InputLib input(rendering);
	se::GUILib gui(input, audio);
	se::debug::DebugLib debug(gui);

	return 0;
}
