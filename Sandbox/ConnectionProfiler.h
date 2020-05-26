#pragma once

#include "SpehsEngine/Net/Connection.h"
#include <memory>

namespace se
{
	namespace rendering
	{
		class Text;
	}
	class GUIContext;
}


class ConnectionProfiler
{
public:
	ConnectionProfiler(se::GUIContext& guiContext);

	void addConnection(std::shared_ptr<se::net::Connection>& connection);

	/* Update needs to be called continuously over time so that data can be collected. */
	void update();

	void setTextColor(const se::Color& color);

private:

	se::GUIContext& guiContext;

	std::vector<std::shared_ptr<se::net::Connection>> connections;
	se::rendering::Text &text;
	boost::signals2::scoped_connection incomingConnectionConnection;
};
