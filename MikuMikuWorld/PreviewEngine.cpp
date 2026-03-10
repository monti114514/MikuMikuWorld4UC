#include "PreviewEngine.h"
#include "Constants.h"
#include "Tempo.h"
#include <algorithm> // std::stable_sort のため
#include <vector>

namespace MikuMikuWorld
{
	SpriteTransform::SpriteTransform(float v[64]) : xx(v), xy(nullptr), yx(nullptr), yy(v + 48) 
	{
		DirectX::XMMATRIX tmp(v + 16);
		if (!DirectX::XMMatrixIsNull(tmp))
			xy = std::make_unique<DirectX::XMMATRIX>(tmp);
		tmp = DirectX::XMMATRIX{v + 32};
		if (!DirectX::XMMatrixIsNull(tmp))
			yx = std::make_unique<DirectX::XMMATRIX>(tmp);
	}

	std::array<DirectX::XMFLOAT4, 4> SpriteTransform::apply(const std::array<DirectX::XMFLOAT4, 4> &vPos) const
	{
		DirectX::XMVECTOR x = DirectX::XMVectorSet(vPos[0].x, vPos[1].x, vPos[2].x, vPos[3].x);
		DirectX::XMVECTOR y = DirectX::XMVectorSet(vPos[0].y, vPos[1].y, vPos[2].y, vPos[3].y);
		DirectX::XMVECTOR
			tx = !xy ? DirectX::XMVector4Transform(x, xx) : DirectX::XMVectorAdd(DirectX::XMVector4Transform(x, xx), DirectX::XMVector4Transform(x, *xy)),
			ty = !yx ? DirectX::XMVector4Transform(y, yy) : DirectX::XMVectorAdd(DirectX::XMVector4Transform(y, *yx), DirectX::XMVector4Transform(y, yy));
		return {{
			{ DirectX::XMVectorGetX(tx), DirectX::XMVectorGetX(ty), vPos[0].z, vPos[0].z },
			{ DirectX::XMVectorGetY(tx), DirectX::XMVectorGetY(ty), vPos[1].z, vPos[1].z },
			{ DirectX::XMVectorGetZ(tx), DirectX::XMVectorGetZ(ty), vPos[2].z, vPos[2].z },
			{ DirectX::XMVectorGetW(tx), DirectX::XMVectorGetW(ty), vPos[3].z, vPos[3].z }
		}};
	}
}

namespace MikuMikuWorld::Engine
{
	// ★ ハイスピード対応の心臓部
	double accumulateScaledDuration(int tick, int beatTicks, const std::vector<Tempo>& tempos, const std::unordered_map<id_t, HiSpeedChange>& hiSpeeds)
	{
		if (hiSpeeds.empty())
			return accumulateDuration(tick, beatTicks, tempos);

		// 1. unordered_mapからvectorに変換し、Tick順に並べ替える
		std::vector<HiSpeedChange> hsList;
		hsList.reserve(hiSpeeds.size());
		for (const auto& [id, hs] : hiSpeeds)
			hsList.push_back(hs);

		std::stable_sort(hsList.begin(), hsList.end(), [](const HiSpeedChange& a, const HiSpeedChange& b) {
			return a.tick < b.tick;
		});

		double duration = 0.0;
		int currentTick = 0;
		int tempoIdx = 0;
		int hsIdx = 0;

		double currentBPM = tempos.empty() ? 120.0 : tempos[0].bpm;
		float currentSpeed = 1.0f; 

		// 2. 目標のTickに到達するまで、BPMとハイスピードの区間ごとに時間を積分する
		while (currentTick < tick)
		{
			// 現在のTickに適用されるBPMを更新
			while (tempoIdx + 1 < tempos.size() && tempos[tempoIdx + 1].tick <= currentTick)
			{
				tempoIdx++;
				currentBPM = tempos[tempoIdx].bpm;
			}

			// 現在のTickに適用されるハイスピードを更新
			while (hsIdx < hsList.size() && hsList[hsIdx].tick <= currentTick)
			{
				currentSpeed = hsList[hsIdx].speed;
				hsIdx++;
			}

			// 次にBPMかハイスピードが変化するTickを計算
			int nextTempoTick = (tempoIdx + 1 < tempos.size()) ? tempos[tempoIdx + 1].tick : tick;
			int nextHSTick = (hsIdx < hsList.size()) ? hsList[hsIdx].tick : tick;

			int nextBoundary = std::min({nextTempoTick, nextHSTick, tick});
            
			if (nextBoundary > currentTick)
			{
				double deltaTicks = (double)(nextBoundary - currentTick);
				// 視覚的時間を進める： (小節の割合) * (1拍あたりの秒数) * (ハイスピード倍率)
				duration += (deltaTicks / beatTicks) * (60.0 / currentBPM) * currentSpeed;
				currentTick = nextBoundary;
			}
			else
			{
				break; // 無限ループ防止
			}
		}

		return duration;
	}

	Range getNoteVisualTime(Note const& note, Score const& score, float noteSpeed)
	{
		double targetTime = accumulateScaledDuration(note.tick, TICKS_PER_BEAT, score.tempoChanges, score.hiSpeedChanges);
		return {targetTime - getNoteDuration(noteSpeed), targetTime};
	}

	std::array<DirectX::XMFLOAT4, 4> quadvPos(float left, float right, float top, float bottom)
	{
		return {{
			{ right, bottom, 0.0f, 1.0f },
			{ right,    top, 0.0f, 1.0f },
			{  left,    top, 0.0f, 1.0f },
			{  left, bottom, 0.0f, 1.0f }
		}};
	}

	std::array<DirectX::XMFLOAT4, 4> perspectiveQuadvPos(float left, float right, float top, float bottom)
	{
		float x1 = right * top,    y1 = top,
				x2 = right * bottom, y2 = bottom,
				x3 = left * bottom,  y3 = bottom,
				x4 = left * top,     y4 = top;
		return {{
			{ x1, y1, 0.0f, 1.0f },
			{ x2, y2, 0.0f, 1.0f },
			{ x3, y3, 0.0f, 1.0f },
			{ x4, y4, 0.0f, 1.0f }
		}};
	}

	std::array<DirectX::XMFLOAT4, 4> perspectiveQuadvPos(float leftStart, float leftStop, float rightStart, float rightStop, float top, float bottom)
	{
		float 
			x1 = rightStart * top,   y1 = top,
			x2 = rightStop * bottom, y2 = bottom,
			x3 = leftStop * bottom,  y3 = bottom,
			x4 = leftStart * top,    y4 = top;
		return {{
			{ x1, y1, 0.0f, 1.0f },
			{ x2, y2, 0.0f, 1.0f },
			{ x3, y3, 0.0f, 1.0f },
			{ x4, y4, 0.0f, 1.0f }
		}};
	}

	std::array<DirectX::XMFLOAT4, 4> quadUV(const Sprite& sprite, const Texture &texture)
	{
		float left = sprite.getX() / texture.getWidth();
		float right = (sprite.getX() + sprite.getWidth()) / texture.getWidth();
		float top = sprite.getY() / texture.getHeight();
		float bottom = (sprite.getY() + sprite.getHeight()) / texture.getHeight();
		return quadvPos(left, right, top, bottom);
	}
}