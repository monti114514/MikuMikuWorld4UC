#include "PreviewEngine.h"
#include "Constants.h"
#include "Tempo.h"

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
	// 【追加】仮の計算関数（Hi-Speed対応は後回しにして、まずは通常の時間計算を返す）
	double accumulateScaledDuration(int tick, int beatTicks, const std::vector<Tempo>& tempos, const std::unordered_map<id_t, HiSpeedChange>& hiSpeeds)
	{
		// TODO: 将来的にMMW4UC独自の高度なHi-Speed計算をここに実装する
		return accumulateDuration(tick, beatTicks, tempos);
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
		// 【修正】MMW4UCのSprite仕様（getXとgetWidth）を用いた座標計算に変更
		float left = sprite.getX() / texture.getWidth();
		float right = (sprite.getX() + sprite.getWidth()) / texture.getWidth();
		float top = sprite.getY() / texture.getHeight();
		float bottom = (sprite.getY() + sprite.getHeight()) / texture.getHeight();
		return quadvPos(left, right, top, bottom);
	}
}