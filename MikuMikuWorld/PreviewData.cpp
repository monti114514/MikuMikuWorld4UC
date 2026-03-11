#include <queue>
#include <stdexcept>
#include "PreviewData.h"
#include "PreviewEngine.h"
#include "Tempo.h"
#include "ApplicationConfiguration.h"
#include "Constants.h"
#include "ResourceManager.h"
#include "ScoreContext.h"
#include <algorithm>

namespace MikuMikuWorld::Engine
{
	double LayerHiSpeedCache::getStm(int tick) const
	{
		if (nodes.empty()) return 0.0;
		auto it = std::upper_bound(nodes.begin(), nodes.end(), tick, [](int t, const HiSpeedCacheNode& node) {
			return t < node.tick;
		});
		if (it != nodes.begin()) --it;
		return it->stm + (tick - it->tick) * it->speedPerTick;
	}

	struct DrawingHoldStep
	{
		int tick;
		double time;
		float left;
		float right;
		EaseType ease;
	};

	static void addHoldNote(DrawData& drawData, const HoldNote& holdNote, Score const &score);

	void DrawData::calculateDrawData(Score const &score)
	{
		this->clear();
		try
		{
			hsCache.clear();
			hsCache.resize(score.layers.size());
			for (int layer = 0; layer < score.layers.size(); ++layer)
			{
				std::vector<HiSpeedChange> hsList;
				for (const auto& [id, hs] : score.hiSpeedChanges)
					if (hs.layer == layer) hsList.push_back(hs);
				
				// ★ ここにソートを追加しました（これがないとハイスピードが正常に適用されません）
				std::sort(hsList.begin(), hsList.end(), [](const HiSpeedChange& a, const HiSpeedChange& b) {
					return a.tick < b.tick;
				});

				// テンポとHSが変化する全てのTick（境界点）を収集
				std::vector<int> boundaries;
				boundaries.push_back(0);
				for (const auto& tempo : score.tempoChanges) boundaries.push_back(tempo.tick);
				for (const auto& hs : hsList) boundaries.push_back(hs.tick);
				std::sort(boundaries.begin(), boundaries.end());
				boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

				// 各境界点における「正確な視覚的時間」と「次の境界までの速度」を計算して保存
				for (int tick : boundaries)
				{
					double stm = accumulateScaledDuration(tick, TICKS_PER_BEAT, score.tempoChanges, score.hiSpeedChanges, layer);
					
					double bpm = 120.0;
					for (auto it = score.tempoChanges.rbegin(); it != score.tempoChanges.rend(); ++it)
						if (it->tick <= tick) { bpm = it->bpm; break; }

					double speed = 1.0;
					for (auto it = hsList.rbegin(); it != hsList.rend(); ++it)
						if (it->tick <= tick) { speed = it->speed; break; }

					double speedPerTick = (60.0 / bpm) * speed / TICKS_PER_BEAT;
					hsCache[layer].nodes.push_back({ tick, stm, speedPerTick });
				}
			}

			this->noteSpeed = 10.0f;

			std::map<int, Range> simBuilder;
			
			for (const auto& [id, note] : score.notes)
			{
				if (note.layer >= 0 && note.layer < score.layers.size() && score.layers[note.layer].hidden)
					continue;

				maxTicks = std::max(note.tick, maxTicks);
				NoteType type = note.getType();
				
				if (type == NoteType::HoldMid
					|| (type == NoteType::Hold && score.holdNotes.at(id).startType != HoldNoteType::Normal)
					|| (type == NoteType::HoldEnd && score.holdNotes.at(note.parentID).endType != HoldNoteType::Normal))
					continue;
				if (type == NoteType::HoldMid)
					continue;
					
				auto visual_tm = getNoteVisualTime(note, score, noteSpeed);
				drawingNotes.push_back(DrawingNote{note.ID, visual_tm, type, note.dummy, note.layer});

				float center = getNoteCenter(note);
				auto&& [it, has_emplaced] = simBuilder.try_emplace(note.tick, Range{center, center});
				auto& x_range = it->second;
				if (has_emplaced)
					continue;
				if (center < x_range.min)
					x_range.min = center;
				if (center > x_range.max)
					x_range.max = center;
			}

			float noteDuration = getNoteDuration(noteSpeed);
			for (const auto& [line_tick, x_range] : simBuilder)
			{
				if (x_range.min != x_range.max)
				{
					// 同時押しラインはとりあえずレイヤー0を基準とする
					double targetTime = accumulateScaledDuration(line_tick, TICKS_PER_BEAT, score.tempoChanges, score.hiSpeedChanges, 0);
					// 末尾に line_tick を追加
					drawingLines.push_back(DrawingLine{ x_range, Range{ targetTime - getNoteDuration(noteSpeed), targetTime }, line_tick });
				}
			}

			for (const auto& [id, holdNote] : score.holdNotes)
			{
				addHoldNote(*this, holdNote, score);
			}
		}
		catch(const std::out_of_range& ex)
		{
			this->clear();
		}
	}

	void DrawData::clear()
	{
		drawingLines.clear();
		drawingNotes.clear();
		drawingHoldTicks.clear();
		drawingHoldSegments.clear();

		maxTicks = 1;
	}

	void addHoldNote(DrawData &drawData, const HoldNote &holdNote, Score const &score)
	{
		const Note& startNote = score.notes.at(holdNote.start.ID);
		const Note& endNote = score.notes.at(holdNote.end);

		if (startNote.layer >= 0 && startNote.layer < score.layers.size() && score.layers[startNote.layer].hidden)
			return;

		float noteDuration = getNoteDuration(drawData.noteSpeed);
		float activeTime = accumulateDuration(startNote.tick, TICKS_PER_BEAT, score.tempoChanges);
		float startTime = activeTime;
		DrawingHoldStep head = {
			startNote.tick,
			accumulateScaledDuration(startNote.tick, TICKS_PER_BEAT, score.tempoChanges, score.hiSpeedChanges, startNote.layer),
			Engine::laneToLeft(startNote.lane),
			Engine::laneToLeft(startNote.lane) + startNote.width,
			holdNote.start.ease
		};
		
		for (ptrdiff_t headIdx = -1, tailIdx = 0, stepSz = holdNote.steps.size(); headIdx < stepSz; ++tailIdx)
		{
			if (tailIdx < stepSz && holdNote.steps[tailIdx].type == HoldStepType::Skip)
				continue;
			HoldStep tailStep = tailIdx == stepSz ? HoldStep{ holdNote.end, HoldStepType::Hidden } : holdNote.steps[tailIdx];
			const Note& tailNote = score.notes.at(tailStep.ID);
			auto easeFunction = getEaseFunction(head.ease);
			DrawingHoldStep tail = {
				tailNote.tick,
				accumulateScaledDuration(tailNote.tick, TICKS_PER_BEAT, score.tempoChanges, score.hiSpeedChanges, startNote.layer),
				Engine::laneToLeft(tailNote.lane),
				Engine::laneToLeft(tailNote.lane) + tailNote.width,
				tailStep.ease
			};
			float endTime = accumulateDuration(tailNote.tick, TICKS_PER_BEAT, score.tempoChanges);
			
			drawData.drawingHoldSegments.push_back(DrawingHoldSegment {
				holdNote.end, 
				head.ease,
				holdNote.isGuide(),
				holdNote.guideColor,
				holdNote.dummy,
				startNote.layer,
				tailIdx,
				head.time, tail.time,
				head.left, head.right,
				tail.left, tail.right,
				startTime, endTime,
				activeTime,
				head.tick, tail.tick
			});
			startTime = endTime;
			
			while ((headIdx + 1) < tailIdx)
			{
				const HoldStep& skipStep = holdNote.steps[headIdx + 1];
				assert(skipStep.type == HoldStepType::Skip);
				const Note& skipNote = score.notes.at(skipStep.ID);
				if (skipNote.tick > tail.tick)
					break;
				double tickTime = accumulateScaledDuration(skipNote.tick, TICKS_PER_BEAT, score.tempoChanges, score.hiSpeedChanges, startNote.layer);
				double tick_t = unlerpD(head.tick, tail.tick, skipNote.tick);
				float skipLeft = easeFunction(head.left, tail.left, tick_t);
				float skipRight = easeFunction(head.right, tail.right, tick_t);
				
				drawData.drawingHoldTicks.push_back(DrawingHoldTick{
					skipStep.ID,
					skipLeft + (skipRight - skipLeft) / 2,
					Range{tickTime - noteDuration, tickTime},
					holdNote.dummy,
					startNote.layer
				});
				++headIdx;
			}
			if (tailStep.type != HoldStepType::Hidden)
			{
				double tickTime = accumulateScaledDuration(tailNote.tick, TICKS_PER_BEAT, score.tempoChanges, score.hiSpeedChanges, startNote.layer);
				drawData.drawingHoldTicks.push_back(DrawingHoldTick{
					tailNote.ID,
					getNoteCenter(tailNote),
					{tickTime - noteDuration, tickTime},
					holdNote.dummy,
					startNote.layer
				});
			}
			head = tail;
			++headIdx;
		}
	}
}