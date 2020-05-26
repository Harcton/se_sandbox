#pragma once
#include "SpehsEngine/Core/ScopeProfiler.h"
#include <memory>
#include <mutex>


namespace se
{
	namespace rendering
	{
		class Text;
		class Polygon;
	}
	class GUIContext;
}


class ScopeProfilerVisualizer
{
public:

	ScopeProfilerVisualizer(se::GUIContext& guiContext);
	~ScopeProfilerVisualizer();

	void update(const se::time::Time& deltaTime);

	void setRenderState(const bool visible);
	bool getRenderState() const;

	void setEnableUpdate(const bool enabled);
	bool getEnableUpdate() const;
	
	/* Set an override for preferred root section length. For example at 1/60 of a second when the root section is the frame. Used for section colouring purposes. */
	void setTargetRootSectionWidth(const se::time::Time* const time);
	void setTimeWindowWidth(const se::time::Time& time);
	void translateTimeWindowBegin(const se::time::Time& time);
	void setMaxThreadDataSectionCount(const size_t count);

private:

	struct SectionInfo
	{
		const se::ScopeProfiler::Section* section = nullptr;
		se::rendering::Polygon* parent = nullptr;
		se::rendering::Polygon* rootParent = nullptr;
		se::time::Time beginTime;
		size_t depth = 0;
	};

	void profilerFlushCallback(const se::ScopeProfiler::ThreadData& threadData);
	void updateSectionPolygons();
	void updateSectionTextStrings();

	se::GUIContext& guiContext;
	
	// Visual settings
	float beginX = 5.0f;
	float beginY = 5.0f;
	float width = 0.0f;
	float sectionHeight = 32.0f;
	float horizontalSpeed = 1.0f;
	se::time::Time timeWindowBegin; // Timestamp where the time window begins
	se::time::Time timeWindowWidth = se::time::fromSeconds(1.0f); // Visualized duration
	se::time::Time disableUpdateTime;
	std::optional<se::time::Time> targetRootSectionWidth;

	bool renderState = true;
	bool enableUpdate = true;
	bool firstVisualUpdateDone = false;

	std::thread::id activeThreadId;
	std::unordered_map<std::thread::id, se::ScopeProfiler::ThreadData> threadDatas;
	std::unordered_map<se::rendering::Polygon*, SectionInfo> polygonToSectionInfoLookup;
	se::rendering::Text& tooltipText;
	se::rendering::Polygon& tooltipPolygon;
	std::vector<se::rendering::Text*> sectionTexts;
	std::vector<se::rendering::Polygon*> sectionPolygons;
	boost::signals2::scoped_connection profilerFlushConnection;

	std::recursive_mutex backgroundThreadDataMutex;
	size_t maxThreadDataSectionCount = 0;
	bool backgroundThreadDataUpdated = false;
	std::unordered_map<std::thread::id, se::ScopeProfiler::ThreadData> backgroundThreadDatas;
};
