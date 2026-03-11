#pragma once
#include <random>
#include "Score.h"
#include "Math.h"
// #include "EffectView.h" <- 削除しました

namespace MikuMikuWorld
{
	struct Score;
	struct Note;
	struct ScoreContext;

	// MMW4UCで削除されたRange構造体をプレビューエンジン用に再定義
	struct Range
	{
		double min;
		double max;
	};
}

namespace MikuMikuWorld::Engine
{
	struct DrawingNote
	{
		int refID;
		Range visualTime;
		NoteType type;
		bool dummy;
		int layer;
	};

	struct DrawingLine
	{
		Range xPos;
		Range visualTime;
		int tick;
	};

	struct DrawingHoldTick
	{
		int refID;
		float center;
		Range visualTime;
		bool dummy;
		int layer;
	};

	struct DrawingHoldSegment
	{
		int endID;
		EaseType ease;
		bool isGuide;
		GuideColor color; // 追加: ガイド線の色
		bool dummy;       // 追加
		int layer;        // 追加

		ptrdiff_t tailStepIndex;
		double headTime, tailTime;
		float headLeft, headRight;
		float tailLeft, tailRight;
		float startTime, endTime;
		double activeTime;

		int startTick;
		int endTick;
	};

	struct HiSpeedCacheNode
	{
		int tick;
		double stm;
		double speedPerTick;
	};

	struct LayerHiSpeedCache
	{
		std::vector<HiSpeedCacheNode> nodes;
		double getStm(int tick) const;
	};

	struct DrawData
	{
		float noteSpeed;
		int maxTicks;
		std::vector<DrawingNote> drawingNotes;
		std::vector<DrawingLine> drawingLines;
		std::vector<DrawingHoldTick> drawingHoldTicks;
		std::vector<DrawingHoldSegment> drawingHoldSegments;

		std::vector<LayerHiSpeedCache> hsCache;

		// Effect::EffectView effectView; <- 削除しました

		void clear();
		void calculateDrawData(Score const& score);
	};
}