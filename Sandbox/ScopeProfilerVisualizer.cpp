#include "stdafx.h"
#include "Sandbox/ScopeProfilerVisualizer.h"

#include "SpehsEngine/Core/StringOperations.h"
#include "SpehsEngine/Core/StringUtilityFunctions.h"
#include "SpehsEngine/Core/Shapes.h"
#include "SpehsEngine/Input/InputManager.h"
#include "SpehsEngine/Input/MouseUtilityFunctions.h"
#include "SpehsEngine/Input/Key.h"
#include "SpehsEngine/Net/ConnectionManager.h"
#include "SpehsEngine/GUI/GUIContext.h"
#include "SpehsEngine/Rendering/BatchManager.h"
#include "SpehsEngine/Rendering/FontManager.h"
#include "SpehsEngine/Rendering/Font.h"
#include "SpehsEngine/Rendering/Polygon.h"
#include "SpehsEngine/Rendering/Text.h"
#include "SpehsEngine/Rendering/Window.h"
#pragma optimize("", off)


ScopeProfilerVisualizer::ScopeProfilerVisualizer(se::GUIContext& _guiContext)
	: guiContext(_guiContext)
	, tooltipText(*_guiContext.getBatchManager().createText(20001))
	, tooltipPolygon(*_guiContext.getBatchManager().createPolygon(se::Shape::BUTTON, 20000, 1.0f, 1.0f))
	, width(float(guiContext.getWindow().getWidth()) - 10.0f)
	, activeThreadId(std::this_thread::get_id())
{
	setMaxThreadDataSectionCount(64);
	se::ScopeProfiler::connectToFlushSignal(profilerFlushConnection, boost::bind(&ScopeProfilerVisualizer::profilerFlushCallback, this, boost::placeholders::_1));

	if (se::rendering::Font* const font = guiContext.getBatchManager().fontManager.getFont("Fonts/Anonymous.ttf"/**/, 12))
	{
		tooltipText.setFont(font);
		tooltipText.setCameraMatrixState(false);
		tooltipText.setColor(se::Color(1.0f, 1.0f, 1.0f));
	}
	tooltipPolygon.setColor(se::Color(0.2f, 0.2f, 0.2f));
	tooltipPolygon.setCameraMatrixState(false);
}

ScopeProfilerVisualizer::~ScopeProfilerVisualizer()
{
	tooltipText.destroy();
	for (se::rendering::Polygon* const polygon : sectionPolygons)
	{
		polygon->destroy();
	}
}

void ScopeProfilerVisualizer::update(const se::time::Time& deltaTime)
{
	SE_SCOPE_PROFILER();

	if (guiContext.getInputManager().isKeyPressed(unsigned(se::input::Key::KP_DIVIDE)))
	{
		setRenderState(!getRenderState());
	}
	if (guiContext.getInputManager().isKeyPressed(unsigned(se::input::Key::KP_MULTIPLY)))
	{
		setEnableUpdate(!getEnableUpdate());
	}
	if (guiContext.getInputManager().isKeyPressed(unsigned(se::input::Key::KP_MINUS)))
	{
		setTimeWindowWidth(std::max(1ll, timeWindowWidth.value << 1));
	}
	if (guiContext.getInputManager().isKeyPressed(unsigned(se::input::Key::KP_PLUS)))
	{
		setTimeWindowWidth(std::max(1ll, timeWindowWidth.value >> 1));
	}
	if (guiContext.getInputManager().isKeyDown(unsigned(se::input::Key::KP_4)))
	{
		translateTimeWindowBegin(-horizontalSpeed * deltaTime.asSeconds() * timeWindowWidth);
	}
	if (guiContext.getInputManager().isKeyDown(unsigned(se::input::Key::KP_6)))
	{
		translateTimeWindowBegin(horizontalSpeed * deltaTime.asSeconds() * timeWindowWidth);
	}
	if (guiContext.getInputManager().isKeyPressed(unsigned(se::input::Key::KP_5)))
	{
		timeWindowBegin = enableUpdate ? se::time::getProfilerTimestamp() : disableUpdateTime;
		translateTimeWindowBegin(-timeWindowWidth / 2);
	}
	if (guiContext.getInputManager().isKeyPressed(unsigned(se::input::Key::KP_0)))
	{
		if (!threadDatas.empty())
		{
			std::unordered_map<std::thread::id, se::ScopeProfiler::ThreadData>::const_iterator it = threadDatas.find(activeThreadId);
			if (it != threadDatas.end())
			{
				it++;
			}
			if (it == threadDatas.end())
			{
				it = threadDatas.begin();
			}
			activeThreadId = it->first;

			// Update if update is disabled
			if (!enableUpdate)
			{
				std::lock_guard<std::recursive_mutex> lock(backgroundThreadDataMutex);
				if (backgroundThreadDataUpdated)
				{
					backgroundThreadDataUpdated = false;
					threadDatas = backgroundThreadDatas;
					updateSectionPolygons();
				}
			}
		}
	}

	if (renderState)
	{
		// Update thread data?
		if (enableUpdate)
		{
			timeWindowBegin += deltaTime;

			std::lock_guard<std::recursive_mutex> lock(backgroundThreadDataMutex);
			if (backgroundThreadDataUpdated)
			{
				backgroundThreadDataUpdated = false;
				threadDatas = backgroundThreadDatas;
				updateSectionPolygons();
			}
		}

		if (!firstVisualUpdateDone)
		{
			firstVisualUpdateDone = true;
			timeWindowBegin = se::time::now() - timeWindowWidth;
		}

		const std::unordered_map<std::thread::id, se::ScopeProfiler::ThreadData>::const_iterator threadDataIt = threadDatas.find(activeThreadId);
		const se::ScopeProfiler::ThreadData* const threadData = threadDataIt != threadDatas.end() ? &threadDataIt->second : nullptr;

		// Element update
		const se::time::Time now = enableUpdate ? se::time::getProfilerTimestamp() : disableUpdateTime;
		const se::time::Time endTime = timeWindowBegin + timeWindowWidth; // Visualized history ends
		const se::time::Time beginTime = endTime - timeWindowWidth; // Visualized history begins
		const glm::vec2 mousePosition = se::input::getMousePositionf();

		tooltipText.setRenderState(false);
		tooltipPolygon.setRenderState(false);
		for (se::rendering::Polygon* const polygon : sectionPolygons)
		{
			const std::unordered_map<se::rendering::Polygon*, SectionInfo>::const_iterator it = polygonToSectionInfoLookup.find(polygon);
			if (it == polygonToSectionInfoLookup.end())
			{
				se::log::error("Polygon key not found from the polygonToSectionInfoLookup.");
				continue;
			}

			const SectionInfo& sectionInfo = it->second;
			if (sectionInfo.beginTime > endTime)
			{
				polygon->setRenderState(false);
				continue;
			}

			const se::ScopeProfiler::Section& section = *sectionInfo.section;
			const se::time::Time sectionEndTime = section.endTime ? *section.endTime : now;
			if (sectionEndTime < beginTime)
			{
				polygon->setRenderState(false);
				continue;
			}

			const std::unordered_map<se::rendering::Polygon*, SectionInfo>::const_iterator parentIt = polygonToSectionInfoLookup.find(sectionInfo.parent);
			const se::time::Time parentSectionBeginTime = parentIt != polygonToSectionInfoLookup.end()
				? parentIt->second.beginTime
				: se::time::Time::zero;
			const se::time::Time parentSectionEndTime = parentIt != polygonToSectionInfoLookup.end()
				? (parentIt->second.section->endTime ? *parentIt->second.section->endTime : now)
				: se::time::Time::zero;
			const se::time::Time parentSectionDuration = parentSectionEndTime - parentSectionBeginTime;
			const std::unordered_map<se::rendering::Polygon*, SectionInfo>::const_iterator rootParentIt = polygonToSectionInfoLookup.find(sectionInfo.rootParent);
			const se::time::Time rootParentSectionBeginTime = rootParentIt != polygonToSectionInfoLookup.end()
				? rootParentIt->second.beginTime
				: se::time::Time::zero;
			const se::time::Time rootParentSectionEndTime = rootParentIt != polygonToSectionInfoLookup.end()
				? (rootParentIt->second.section->endTime ? *rootParentIt->second.section->endTime : now)
				: se::time::Time::zero;
			const se::time::Time rootParentSectionDuration = rootParentSectionEndTime - rootParentSectionBeginTime;
			const se::time::Time visualCompareSectionDuration = targetRootSectionWidth ? *targetRootSectionWidth : rootParentSectionDuration;
			const se::time::Time sectionBeginTime = sectionInfo.beginTime;
			const se::time::Time sectionDuration = sectionEndTime - sectionBeginTime;
			const se::time::Time displaySectionBeginTime = std::max(beginTime, sectionBeginTime);
			const se::time::Time displaySectionEndTime = std::min(endTime, sectionEndTime);
			const se::time::Time displaySectionDuration = displaySectionEndTime - displaySectionBeginTime;
			se_assert(displaySectionDuration >= se::time::Time::zero);
			const float xStartPercentage = (displaySectionBeginTime - beginTime).asSeconds() / timeWindowWidth.asSeconds();
			const float xEndPercentage = (displaySectionEndTime - beginTime).asSeconds() / timeWindowWidth.asSeconds();
			const float timeWindowPercentage = xEndPercentage - xStartPercentage;
			const glm::vec2 position(beginX + width * xStartPercentage, beginY + sectionInfo.depth * sectionHeight);
			const glm::vec2 size(std::max(1.0f, timeWindowPercentage * width), sectionHeight);
			const float percentageOfParent = sectionInfo.parent ? (sectionDuration.asSeconds() / parentSectionDuration.asSeconds()) : 0.0f;
			const float percentageOfVisualCompareSection = sectionDuration.asSeconds() / visualCompareSectionDuration.asSeconds();
			const float positiveColorFactor = percentageOfVisualCompareSection * 0.25f;
			polygon->setRenderState(true);
			polygon->setPosition(position);
			polygon->setScale(size);
			polygon->setColor(se::Color(1.0f, 0.0f, 0.0f) * positiveColorFactor + se::Color(0.0f, 1.0f, 0.0f) * (1.0f - positiveColorFactor));
			
			// Tooltip hover?
			if (mousePosition.x >= position.x && mousePosition.x <= (position.x + size.x) &&
				mousePosition.y >= position.y && mousePosition.y <= (position.y + size.y))
			{
				std::string string;
				string += "Name: " + section.name;
				string += "\nFunction: " + std::string(section.function);
				string += "\nFile: " + std::string(section.file);
				string += "\nLine: " + std::to_string(section.line);
				string += "\nLength: " + se::toTimeLengthString(sectionDuration, 6);
				if (sectionInfo.parent)
				{
					string += " (" + se::toString(percentageOfParent * 100.0f, 2) + "% of parent)";
				}
				if (threadData)
				{
					std::stringstream stringstream;
					stringstream << threadData->threadId;
					string += "\nThread id: " + std::string(stringstream.str());
				}
				tooltipText.setString(string);
				tooltipText.setRenderState(true);
				const float textWidth = tooltipText.getTextWidth();
				const float tooltipPolygonBorder = 2.0f;
				const float tooltipWidth = textWidth + 2.0f * tooltipPolygonBorder;
				const glm::vec2 tooltipPosition(std::min(mousePosition.x + tooltipWidth, float(guiContext.getWindow().getWidth())) - tooltipWidth, mousePosition.y);
				tooltipText.setPosition(tooltipPosition + glm::vec2(tooltipPolygonBorder, tooltipPolygonBorder));
				tooltipPolygon.setRenderState(true);
				tooltipPolygon.setScaleX(tooltipWidth);
				tooltipPolygon.setScaleY(tooltipText.getTextHeight() + 2.0f * tooltipPolygonBorder);
				tooltipPolygon.setPosition(tooltipPosition);
			}
		}
	}
}

void ScopeProfilerVisualizer::updateSectionPolygons()
{
	SE_SCOPE_PROFILER();

	polygonToSectionInfoLookup.clear();
	const std::unordered_map<std::thread::id, se::ScopeProfiler::ThreadData>::const_iterator it = threadDatas.find(activeThreadId);
	if (it != threadDatas.end())
	{
		const se::ScopeProfiler::ThreadData& threadData = it->second;

		// Generate section polygons
		const size_t sectionCount = threadData.getSectionCountRecursive();
		if (sectionPolygons.size() > sectionCount)
		{
			const size_t removeCount = sectionPolygons.size() - sectionCount;
			for (size_t i = 0; i < removeCount; i++)
			{
				sectionPolygons[sectionPolygons.size() - i - 1]->destroy();
			}
			sectionPolygons.erase(sectionPolygons.end() - removeCount, sectionPolygons.end());
		}
		else
		{
			while (sectionPolygons.size() < sectionCount)
			{
				sectionPolygons.push_back(guiContext.getBatchManager().createPolygon(se::Shape::BUTTON, 0, 1.0f, 1.0f));
				sectionPolygons.back()->setCameraMatrixState(false);
				sectionPolygons.back()->setColor(se::Color(0.0f, 1.0f, 0.0f));
			}
		}

		if (!threadData.sections.empty())
		{
			size_t sectionIndex = 0u;
			std::function<void(const se::time::Time, const se::ScopeProfiler::Section&, const size_t, se::rendering::Polygon* const, se::rendering::Polygon* const)> updatePolygon;
			updatePolygon = [&updatePolygon, &sectionIndex, this]
			(const se::time::Time sectionBeginTime, const se::ScopeProfiler::Section& section, const size_t depth, se::rendering::Polygon* const parent, se::rendering::Polygon* const rootParent)
			{
				se::rendering::Polygon& polygon = *sectionPolygons[sectionIndex++];
				SectionInfo& sectionInfo = polygonToSectionInfoLookup[&polygon];
				sectionInfo.section = &section;
				sectionInfo.parent = parent;
				sectionInfo.rootParent = rootParent;
				sectionInfo.beginTime = sectionBeginTime;
				sectionInfo.depth = depth;
				for (const std::pair<const se::time::Time, se::ScopeProfiler::Section>& pair : section.children)
				{
					updatePolygon(pair.first, pair.second, depth + 1, &polygon, rootParent ? rootParent : &polygon);
				}
			};

			size_t depth = 0;
			for (const std::pair<const se::time::Time, se::ScopeProfiler::Section>& pair : threadData.sections)
			{
				updatePolygon(pair.first, pair.second, depth, nullptr, nullptr);
			}
			while (sectionIndex < sectionPolygons.size())
			{
				sectionPolygons[sectionIndex++]->setRenderState(false);
			}
		}
	}
	else
	{
		for (se::rendering::Polygon* const polygon : sectionPolygons)
		{
			polygon->destroy();
		}
		sectionPolygons.clear();
	}
}

void ScopeProfilerVisualizer::updateSectionTextStrings()
{
	//std::stringstream stringstream;
	//stringstream << "Threads: " + std::to_string(threadDatas.size());
	//for (const std::pair<std::thread::id, se::Profiler::ThreadData> threadDataPair : threadDatas)
	//{
	//	stringstream << "\n    Thread id: " << threadDataPair.first;
	//	for (const std::pair<se::time::Time, se::Profiler::Section>& sectionPair : threadDataPair.second.sections)
	//	{
	//		const se::time::Time duration = sectionPair.second.endTime - sectionPair.first;
	//		stringstream << "\n        " << sectionPair.second.name << ": " << std::to_string(duration.asNanoseconds()) << " ns";
	//	}
	//}

	//text.setString(stringstream.str());
}

void ScopeProfilerVisualizer::setTargetRootSectionWidth(const se::time::Time* const time)
{
	if (time)
	{
		targetRootSectionWidth.emplace(*time);
	}
	else
	{
		targetRootSectionWidth.reset();
	}
}

void ScopeProfilerVisualizer::setTimeWindowWidth(const se::time::Time& time)
{
	if (time != timeWindowWidth)
	{
		const se::time::Time delta = time - timeWindowWidth;
		const se::time::Time halfDelta = delta / 2ll;
		translateTimeWindowBegin(-halfDelta);
		timeWindowWidth = time;
	}
}

void ScopeProfilerVisualizer::translateTimeWindowBegin(const se::time::Time& time)
{
	const se::time::Time newTimeWindowBegin = timeWindowBegin + time;
	if (time < se::time::Time::zero)
	{
		if (newTimeWindowBegin < timeWindowBegin)
		{
			timeWindowBegin = newTimeWindowBegin;
		}
		else
		{
			timeWindowBegin.value = std::numeric_limits<decltype(timeWindowBegin.value)>::min();
		}
	}
	else
	{
		if (newTimeWindowBegin > timeWindowBegin)
		{
			timeWindowBegin = newTimeWindowBegin;
		}
		else
		{
			timeWindowBegin.value = std::numeric_limits<decltype(timeWindowBegin.value)>::max();
		}
	}
}

void ScopeProfilerVisualizer::setMaxThreadDataSectionCount(const size_t count)
{
	if (maxThreadDataSectionCount != count)
	{
		maxThreadDataSectionCount = count;
	}
}

void ScopeProfilerVisualizer::setRenderState(const bool visible)
{
	renderState = visible;
	tooltipText.setRenderState(visible);
	for (se::rendering::Text *const text : sectionTexts)
	{
		text->setRenderState(visible);
	}
	if (!visible)
	{
		tooltipText.setRenderState(visible);
		tooltipPolygon.setRenderState(visible);
		for (se::rendering::Polygon* const polygon : sectionPolygons)
		{
			polygon->setRenderState(visible);
		}
	}
}

bool ScopeProfilerVisualizer::getRenderState() const
{
	return renderState;
}

void ScopeProfilerVisualizer::setEnableUpdate(const bool enabled)
{
	if (enabled != enableUpdate)
	{
		enableUpdate = enabled;
		if (enabled)
		{
			const se::time::Time timeSinceDisableUpdate = se::time::now() - disableUpdateTime;
			timeWindowBegin += timeSinceDisableUpdate;
		}
		else
		{
			disableUpdateTime = se::time::now();
		}
	}
}

bool ScopeProfilerVisualizer::getEnableUpdate() const
{
	return enableUpdate;
}

void ScopeProfilerVisualizer::profilerFlushCallback(const se::ScopeProfiler::ThreadData& _threadData)
{
	std::lock_guard<std::recursive_mutex> lock(backgroundThreadDataMutex);
	std::unordered_map<std::thread::id, se::ScopeProfiler::ThreadData>::iterator it = backgroundThreadDatas.find(_threadData.threadId);
	if (it != backgroundThreadDatas.end())
	{
		it->second.add(_threadData);
	}
	else
	{
		backgroundThreadDatas[_threadData.threadId] = _threadData;
	}
	backgroundThreadDatas[_threadData.threadId].truncate(3000);
	backgroundThreadDataUpdated = true;
}
