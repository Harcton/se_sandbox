#include "stdafx.h"

#include "SpehsEngine/Audio/AudioLib.h"
#include "SpehsEngine/Core/CoreLib.h"
#include "SpehsEngine/Core/DeltaTimeSystem.h"
#include "SpehsEngine/Core/StringOperations.h"
#include "SpehsEngine/Core/Inifile.h"
#include "SpehsEngine/Core/Console.h"
#include "SpehsEngine/Core/Thread.h"
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

	se::Inifile inifile("netserver");
	inifile.read();
	se::Inivar<unsigned>& windowWidth = inifile.get("video", "window_width", 800u);
	se::Inivar<unsigned>& windowHeight = inifile.get("video", "window_height", 900u);
	se::Inivar<unsigned>& limitFps = inifile.get("video", "limit_fps", 60u);
	const se::net::Port port(inifile.get("network", "port", uint16_t(41666)));
	inifile.write();
	
	const se::time::Time minFrameTime = se::time::fromSeconds(1.0f / float(limitFps));

	se::GUIRectangle::defaultColor = se::Color(0.2f, 0.2f, 0.2f);
	se::GUIRectangle::defaultStringColor = se::Color(0.9f, 0.9f, 0.9f);

	se::rendering::Window window(windowWidth, windowHeight);
	se::rendering::ShaderManager shaderManager;
	se::rendering::Camera2D camera2D(window);
	se::rendering::BatchManager batchManager2D(window, shaderManager, camera2D);
	window.setTitle("NetServer");
	window.setClearColor(se::Color(0.1f, 0.1f, 0.1f));

	se::input::InputManager inputManager(window);
	se::input::EventCatcher eventCatcher;
	se::input::EventSignaler eventSignaler;
	se::time::DeltaTimeSystem deltaTimeSystem;
	se::GUIContext guiContext(batchManager2D, inputManager, deltaTimeSystem);

	se::net::IOService ioService;
	se::net::ConnectionManager connectionManager(ioService, "server");
	se::rendering::Text& text = *batchManager2D.createText();
	text.setFont("Fonts/Anonymous.ttf", 14);
	text.setColor(se::Color(1.0f, 1.0f, 1.0f));
	const auto updateTextPosition = [&text, &window]()
	{
		const float textHeight = float(std::max(0, text.getLineCount() - 1) * text.getLineSpacing() + text.getLineCount() * text.getFontHeight());		
		text.setPosition(window.getWidth() - text.getTextWidth() - 5.0f, window.getHeight() - 5.0f - textHeight);
	};

	// Console
	se::Console console;
	se::rendering::ConsoleVisualizer consoleVisualizer(console, inputManager, batchManager2D);

	// Connection profiler
	se::debug::ConnectionProfiler connectionProfiler(guiContext);
	boost::signals2::scoped_connection addToConnectionProfilerConnection;
	connectionManager.connectToIncomingConnectionSignal(addToConnectionProfilerConnection, [&connectionProfiler](std::shared_ptr<se::net::Connection> &connection)
		{
			connectionProfiler.addConnection(connection);
		});

	connectionManager.setDebugLogLevel(1);
	connectionManager.bind(port);
	connectionManager.startAccepting();

	if (true)
	{
		// Continuous file transfer
		struct Connection
		{
			uint64_t dataIndex = 0u;
			std::shared_ptr<se::net::Connection> connection;
		};
		std::vector<Connection> connections;
		boost::signals2::scoped_connection incomingConnection;
		connectionManager.connectToIncomingConnectionSignal(incomingConnection, [&guiContext, &connections](std::shared_ptr<se::net::Connection>& connection)
			{
				connections.push_back(Connection());
				connections.back().connection = connection;
				se::log::info("Server: incoming connection accepted: " + connection->debugEndpoint);
			});

		uint64_t targetBytesPerSecond = 1024;
		const se::time::Time sendInterval = se::time::fromSeconds(1.0f / 30.0f);
		se::time::Time lastSendTime;
		while (true)
		{
			const se::time::ScopedFrameLimiter frameLimiter(minFrameTime);

			connectionManager.update();
			for (Connection& connection : connections)
			{
				if (se::time::now() - lastSendTime > sendInterval)
				{
					se::WriteBuffer writeBuffer;
					const size_t count = size_t(float(targetBytesPerSecond) * sendInterval.asSeconds());
					for (size_t i = 0; i < count; i++)
					{
						writeBuffer.write(connection.dataIndex++);
					}
					connection.connection->sendPacket(writeBuffer, true);
					lastSendTime = se::time::now();
				}
			}

			//Input
			input.update();
			audio.update();
			eventCatcher.pollEvents();
			eventSignaler.signalEvents(eventCatcher);
			inputManager.update(eventCatcher);

			//Update
			deltaTimeSystem.deltaTimeSystemUpdate();
			inifile.update();
			consoleVisualizer.update(deltaTimeSystem.deltaTime);
			connectionManager.update();
			connectionProfiler.update();
			if (inputManager.isKeyPressed(unsigned(se::input::Key::UP)))
			{
				targetBytesPerSecond = targetBytesPerSecond << 1;
			}
			if (inputManager.isKeyPressed(unsigned(se::input::Key::DOWN)))
			{
				targetBytesPerSecond = targetBytesPerSecond >> 1;
			}
			if (inputManager.isKeyPressed(unsigned(se::input::Key::BACKSPACE)))
			{
				for (Connection& connection : connections)
				{
					connection.connection->resetReliableFragmentSendCounters();
					connection.connection->resetMutexTimes();
				}
			}
			std::string string;
			string = "Target Bps: " + std::to_string(targetBytesPerSecond);
			text.setString(string);
			updateTextPosition();

			//Render
			window.renderBegin();
			consoleVisualizer.render();
			batchManager2D.render();
			window.renderEnd();
		}
	}
	else
	{
		// Single packet transfer
		uint64_t packetSize = 4096;
		struct Connection
		{
			std::shared_ptr<se::net::Connection> connection;
		};
		std::vector<Connection> connections;
		boost::signals2::scoped_connection incomingConnection;
		connectionManager.connectToIncomingConnectionSignal(incomingConnection, [&packetSize, &connections](std::shared_ptr<se::net::Connection>& connection)
			{
				connections.push_back(Connection());
				connections.back().connection = connection;
				se::log::info("Server: incoming connection accepted: " + connection->debugEndpoint);

				// Send data
				se::WriteBuffer writeBuffer;
				uint8_t dataIndex = 0;
				for (uint64_t i = 0; i < packetSize; i++)
				{
					writeBuffer.write(dataIndex++);
				}
				connection->sendPacket(writeBuffer, true);
			});
		while (true)
		{
			const se::time::ScopedFrameLimiter frameLimiter(minFrameTime);

			connectionManager.update();

			//Input
			input.update();
			audio.update();
			eventCatcher.pollEvents();
			eventSignaler.signalEvents(eventCatcher);
			inputManager.update(eventCatcher);

			//Update
			deltaTimeSystem.deltaTimeSystemUpdate();
			inifile.update();
			consoleVisualizer.update(deltaTimeSystem.deltaTime);
			connectionManager.update();
			connectionProfiler.update();
			if (inputManager.isKeyPressed(unsigned(se::input::Key::UP)))
			{
				packetSize = packetSize << 1;
			}
			if (inputManager.isKeyPressed(unsigned(se::input::Key::DOWN)))
			{
				packetSize = packetSize >> 1;
			}
			if (inputManager.isKeyPressed(unsigned(se::input::Key::RETURN)))
			{
				se::WriteBuffer writeBuffer;
				uint8_t dataIndex = 0;
				for (uint64_t i = 0; i < packetSize; i++)
				{
					writeBuffer.write(dataIndex++);
				}
				for (Connection& connection : connections)
				{
					connection.connection->sendPacket(writeBuffer, true);
				}
			}
			if (inputManager.isKeyPressed(unsigned(se::input::Key::BACKSPACE)))
			{
				for (Connection& connection : connections)
				{
					connection.connection->resetReliableFragmentSendCounters();
					connection.connection->resetMutexTimes();
				}
			}
			std::string string;
			if (packetSize > 1024 * 1024 * 1024)
			{
				string += "\nPacket size: " + std::to_string(packetSize / (1024 * 1024 * 1024)) + " GB";
			}
			else if (packetSize > 1024 * 1024)
			{
				string += "\nPacket size: " + std::to_string(packetSize / (1024 * 1024)) + " MB";
			}
			else if (packetSize > 1024)
			{
				string += "\nPacket size: " + std::to_string(packetSize / 1024) + " KB";
			}
			else
			{
				string += "\nPacket size: " + std::to_string(packetSize) + " B";
			}
			text.setString(string);
			updateTextPosition();

			//Render
			window.renderBegin();
			consoleVisualizer.render();
			batchManager2D.render();
			window.renderEnd();
		}
	}

	return 0;
}
