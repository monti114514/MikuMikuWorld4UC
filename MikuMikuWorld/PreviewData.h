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
		NoteType type;  // 追加: ダメージノーツなどの判別用
		bool dummy;     // 追加: ダミー(フェイク)ノーツ判別用
		int layer;      // 追加: レイヤー情報
	};

	struct DrawingLine
	{
		Range xPos;
		Range visualTime;
	};

	struct DrawingHoldTick
	{
		int refID;
		float center;
		Range visualTime;
		bool dummy;     // 追加
		int layer;      // 追加
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
	};

	struct DrawData
	{
		float noteSpeed;
		int maxTicks;
		std::vector<DrawingNote> drawingNotes;
		std::vector<DrawingLine> drawingLines;
		std::vector<DrawingHoldTick> drawingHoldTicks;
		std::vector<DrawingHoldSegment> drawingHoldSegments;
		// Effect::EffectView effectView; <- 削除しました

		void clear();
		void calculateDrawData(Score const& score);
	};
}