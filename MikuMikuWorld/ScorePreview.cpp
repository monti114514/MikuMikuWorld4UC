#include "Application.h"
#include "ScorePreview.h"
#include "PreviewEngine.h"
#include "Rendering/Camera.h"
#include "ResourceManager.h"
#include "Colors.h"
#include "ImageCrop.h"
#include "ApplicationConfiguration.h"
#include "Score.h"
#include "Utilities.h"
#include <glad/glad.h>

namespace MikuMikuWorld
{
	struct PreviewPlaybackState
	{
		bool isPlaying{}, wasLastFramePlaying{};
	} playbackState;

	constexpr float EFFECTS_TARGET_ASPECT = 16.f / 9.f;
	constexpr float MATH_PI = 3.14159265358979323846f; 

	inline DirectX::XMFLOAT4 toFloat4(const Color& c, float alphaScale = 1.0f) {
		return { c.r, c.g, c.b, c.a * alphaScale };
	}

	namespace Utils
	{
		inline void fitRect(float target_width, float target_height, long double source_aspect_ratio, float& width, float& height)
		{
			const float target_aspect_ratio = target_width / target_height;
			width  = target_aspect_ratio > source_aspect_ratio ? (float)source_aspect_ratio * target_height : target_width;
			height = target_aspect_ratio < source_aspect_ratio ? target_width / (float)source_aspect_ratio : target_height;
		}

		inline void fillRect(float target_width, float target_height, long double source_aspect_ratio, float& width, float& height)
		{
			const float target_aspect_ratio = target_width / target_height;
			width  = target_aspect_ratio < source_aspect_ratio ? (float)source_aspect_ratio * target_height : target_width;
			height = target_aspect_ratio > source_aspect_ratio ? target_width / (float)source_aspect_ratio : target_height;
		}

		inline std::array<DirectX::XMFLOAT4, 4> getUV(float left, float right, float top, float bottom)
		{
			return {{
				{ right, top,    0.f, 1.f }, 
				{ right, bottom, 0.f, 1.f }, 
				{ left,  bottom, 0.f, 1.f }, 
				{ left,  top,    0.f, 1.f }  
			}};
		}
	};

	static const int NOTE_SIDE_WIDTH = 91;
	static const int NOTE_SIDE_PAD = 10;
	static const int MAX_FLICK_SPRITES = 6;
	static const int HOLD_XCUTOFF = 36;
	static const int GUIDE_XCUTOFF = 3;
	static const int GUIDE_Y_TOP_CUTOFF = -41;
	static const int GUIDE_Y_BOTTOM_CUTOFF = -12;
	static Color defaultTint { 1.f, 1.f, 1.f, 1.f };

	ScorePreviewBackground::ScorePreviewBackground() : backgroundFile(), jacketFile{}, brightness(0.5f), frameBuffer{2048, 2048}, init{false} {}

	ScorePreviewBackground::~ScorePreviewBackground()
	{
		frameBuffer.dispose();
	}

	void ScorePreviewBackground::setBrightness(float value)
	{
		brightness = value;
	}

	void ScorePreviewBackground::update(Renderer* renderer, const Jacket& jacket)
	{
		init = true;
		backgroundFile = config.backgroundImage;
		jacketFile = jacket.getFilename();
		brightness = config.pvBackgroundBrightness;
		bool useDefaultTexture = backgroundFile.empty() || !IO::File::exists(backgroundFile);
		
		Texture backgroundTex = { useDefaultTexture ? Application::getAppDir() + "res\\textures\\default.png" : backgroundFile};
		const float bgWidth = backgroundTex.getWidth(), bgHeight = backgroundTex.getHeight();
		if (bgWidth != frameBuffer.getWidth() || bgHeight != frameBuffer.getHeight())
			frameBuffer.resize((unsigned int)bgWidth, (unsigned int)bgHeight);
		frameBuffer.bind();
		frameBuffer.clear();
		
		int shaderId;
		if ((shaderId = ResourceManager::getShader("basic2d")) == -1) return;
		Shader* basicShader = ResourceManager::shaders[shaderId];
		
		basicShader->use();

		const float projectionX{ std::max(bgWidth, 10.f) };
		const float projectionY{ std::max(bgHeight, 10.f) };
		basicShader->setMatrix4("projection", Camera().getOffCenterOrthographicProjection(0, projectionX, 0, projectionY)); 
		
		if (backgroundTex.getID() > 0)
		{
			renderer->beginBatch();
			renderer->drawRectangle(Vector2(0, 0), Vector2(bgWidth, bgHeight), backgroundTex, 0, bgWidth, 0, bgHeight, defaultTint, 0);
			renderer->endBatch();
			if (useDefaultTexture && IO::File::exists(jacket.getFilename()))
				updateDrawDefaultJacket(renderer, jacket);
		}
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		backgroundTex.dispose();
	}

	void ScorePreviewBackground::updateDrawDefaultJacket(Renderer* renderer, const Jacket& jacket) {}

	bool ScorePreviewBackground::shouldUpdate(const Jacket& jacket) const
	{
		return !init || backgroundFile != config.backgroundImage || jacketFile != jacket.getFilename();
	}

	void ScorePreviewBackground::draw(Renderer *renderer, float scrWidth, float scrHeight) const
	{
		float bgScrWidth = (float)frameBuffer.getWidth(), bgScrHeight = (float)frameBuffer.getHeight(), targetWidth, targetHeight;
		if (!backgroundFile.empty())
		{
			Utils::fillRect(scrWidth, scrHeight, bgScrWidth / bgScrHeight, bgScrWidth, bgScrHeight);
			targetWidth = Engine::STAGE_TARGET_WIDTH;
			targetHeight = Engine::STAGE_TARGET_HEIGHT;
		}
		else
		{
			bgScrWidth = Engine::BACKGROUND_SIZE;
			bgScrHeight = Engine::BACKGROUND_SIZE;
			targetWidth = Engine::STAGE_TARGET_WIDTH;
			targetHeight = Engine::STAGE_TARGET_HEIGHT;
		}
		
		const float bgWidth = bgScrWidth / (targetWidth * Engine::STAGE_WIDTH_RATIO);
		const float bgLeft = -bgWidth / 2.f;
		const float bgHeight = bgScrHeight / (targetHeight * Engine::STAGE_HEIGHT_RATIO);
		const float centerY = 0.5f / Engine::STAGE_HEIGHT_RATIO + Engine::STAGE_LANE_TOP / Engine::STAGE_LANE_HEIGHT;
		const float bgTop = centerY + -bgHeight / 2.f;
		auto vPos = Engine::quadvPos(bgLeft, bgLeft + bgWidth, bgTop, bgTop + bgHeight);
		auto uv = Utils::getUV(0.f, 1.f, 0.f, 1.f);
		renderer->pushQuad(vPos, uv, DirectX::XMMatrixIdentity(), DirectX::XMFLOAT4(brightness, brightness, brightness, 1.f), frameBuffer.getTexture(), -10);
	}

	std::array<DirectX::XMFLOAT4, 4> ScorePreviewBackground::DefaultJacket::getLeftUV() { return {{ { 303.8f / 740, 504.8f / 740, 0, 0 }, { 317.5f / 740, 297.7f / 740, 0, 0 }, { 5.5f / 740, 278.3f / 740, 0, 0 }, { -8.f / 740, 497.4f / 740, 0, 0 } }}; }
	std::array<DirectX::XMFLOAT4, 4> ScorePreviewBackground::DefaultJacket::getRightUV() { return {{ { 749.5f / 740, 377.7f / 740, 0, 0 }, { 738.2f / 740, 188.1f / 740, 0, 0 }, { 415.0f / 740, 171.4f / 740, 0, 0 }, { 432.1f / 740, 363.9f / 740, 0, 0 } }}; }
	std::array<DirectX::XMFLOAT4, 4> ScorePreviewBackground::DefaultJacket::getLeftMirrorUV() { return {{ { 292.761414f / 740, 247.401382f / 740, 0, 0 }, { 310.765869f / 740, 491.944763f / 740, 0, 0 }, { 6.892246f / 740, 498.470642f / 740, 0, 0 }, { -6.246704f / 740, 258.264862f / 740, 0, 0 } }}; }
	std::array<DirectX::XMFLOAT4, 4> ScorePreviewBackground::DefaultJacket::getRightMirrorUV() { return {{ { 733.444458f / 740, 183.954681f / 740, 0, 0 }, { 743.541321f / 740, 355.960449f / 740, 0, 0 }, { 418.899414f / 740, 332.759491f / 740, 0, 0 }, { 410.746246f / 740, 155.907684f / 740, 0, 0 } }}; }
	std::array<DirectX::XMFLOAT4, 4> ScorePreviewBackground::DefaultJacket::getCenterUV() { return {{ { 755.541687f / 740, 744.057861f / 740, 0, 0 }, { 739.961182f / 740, -1.859504f / 740, 0, 0 }, { 0.043696f / 740, -1.859504f / 740, 0, 0 }, { -17.484388f / 740, 744.057861f / 740, 0, 0 } }}; }
	std::array<DirectX::XMFLOAT4, 4> ScorePreviewBackground::DefaultJacket::getMirrorCenterUV() { return {{ { 747.697083f / 740, 2.164453f / 740, 0, 0 }, { 743.909424f / 740, 731.297241f / 740, 0, 0 }, { -1.864066f / 740, 731.297241f / 740, 0, 0 }, { 3.837242f / 740, 2.164453f / 740, 0, 0 } }}; }

	ScorePreviewWindow::ScorePreviewWindow() : previewBuffer{ 1920, 1080 }, background(), scaledAspectRatio(1) {}

	ScorePreviewWindow::~ScorePreviewWindow() {}

	void ScorePreviewWindow::update(ScoreContext& context, Renderer* renderer)
	{
		bool isWindowActive =  !ImGui::IsWindowDocked() || ImGui::GetCurrentWindow()->TabId == ImGui::GetWindowDockNode()->SelectedTabId;
		if (!isWindowActive) return;

		if (context.scorePreviewDrawData.noteSpeed != config.pvNoteSpeed)
			context.scorePreviewDrawData.calculateDrawData(context.score);
		ImVec2 size = ImGui::GetContentRegionAvail() - ImVec2{ this->getScrollbarWidth(), 0 };
		ImVec2 position = ImGui::GetCursorScreenPos();
		ImRect boundaries = ImRect(position, position + size);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(boundaries.Min, boundaries.Max, 0xff202020);
		
		if (config.drawBackground && background.shouldUpdate(context.workingData.jacket))
			background.update(renderer, context.workingData.jacket);

		static int shaderId = ResourceManager::getShader("basic2d");
		if (shaderId == -1) return;

		Shader* shader = ResourceManager::shaders[shaderId];
		shader->use();

		float width  = size.x, height = size.y;
		float scaledWidth = Engine::STAGE_TARGET_WIDTH * Engine::STAGE_WIDTH_RATIO;
		float scaledHeight = Engine::STAGE_TARGET_HEIGHT * Engine::STAGE_HEIGHT_RATIO;
		float scrTop  = Engine::STAGE_TARGET_HEIGHT * Engine::STAGE_TOP_RATIO;
		Utils::fillRect(Engine::STAGE_TARGET_WIDTH, Engine::STAGE_TARGET_HEIGHT, size.x / size.y, width, height);
		
		scaledAspectRatio = scaledWidth / scaledHeight;

		auto view = DirectX::XMMatrixScaling(scaledWidth, scaledHeight, 1.f) * DirectX::XMMatrixTranslation(0.f, -scrTop, 0.f);
		auto projection = Camera().getOffCenterOrthographicProjection(-width / 2, width / 2, height / 2, -height / 2);
		auto viewProjection = DirectX::XMMatrixMultiply(view, projection); 

		shader->setMatrix4("projection", viewProjection);

		if (previewBuffer.getWidth() != (unsigned int)size.x || previewBuffer.getHeight() != (unsigned int)size.y)
			previewBuffer.resize((unsigned int)size.x, (unsigned int)size.y);
		previewBuffer.bind();
		previewBuffer.clear();

		renderer->beginBatch();
		if (config.drawBackground)
		{
			background.setBrightness(config.pvBackgroundBrightness);
			background.draw(renderer, width, height);
		}
		drawStage(renderer);
		renderer->endBatch();

		shader->use();
		shader->setMatrix4("projection", viewProjection);
		renderer->beginBatch();
		drawLines(context, renderer);
		drawHoldCurves(context, renderer);
		renderer->endBatch(); 

		shader->use();
		shader->setMatrix4("projection", viewProjection);
		renderer->beginBatch();

		drawHoldTicks(context, renderer);
		drawNotes(context, renderer);
		if (config.pvStageCover != 0) {
			drawStageCoverMask(renderer);
			drawStageCover(renderer);
		}
		renderer->endBatch();

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		
		drawList->AddImage((ImTextureID)(size_t)previewBuffer.getTexture(), position, position + size, {0, 0}, {1, 1});
	}

	void ScorePreviewWindow::updateUI(ScoreEditorTimeline& timeline, ScoreContext& context)
	{
		updateToolbar(timeline, context);
		ImGuiIO io = ImGui::GetIO();
		float mouseWheel = io.MouseWheel * 1;
		if (!timeline.isPlaying() && ImGui::IsWindowHovered() && mouseWheel != 0)
			context.currentTick += std::max(mouseWheel * TICKS_PER_BEAT / 2, (float)-context.currentTick);
		updateScrollbar(timeline, context);

		playbackState.wasLastFramePlaying = playbackState.isPlaying;
		playbackState.isPlaying = timeline.isPlaying();

		if (isFullWindow())
		{
			if (ImGui::BeginPopupContextWindow("preview_context_menu", 1 ))
			{
				bool _fullWindow = fullWindow;
				if (ImGui::MenuItem(getString("fullscreen_preview"), NULL, &_fullWindow))
					setFullWindow(_fullWindow);

				ImGui::MenuItem(getString("preview_draw_toolbar"), NULL, &config.pvDrawToolbar);
				ImGui::MenuItem(getString("return_to_last_tick"), NULL, &config.returnToLastSelectedTickOnPause);
				ImGui::EndPopup();
			}
		}
	}

	void ScorePreviewWindow::setFullWindow(bool _fullWindow)
	{
		fullWindow = _fullWindow;
	}

	const Texture &ScorePreviewWindow::getNoteTexture()
	{
		return ResourceManager::textures[noteTextures.notes];
	}

	void ScorePreviewWindow::drawStage(Renderer* renderer)
	{
		int index = ResourceManager::getTexture("stage");
		if (index == -1) return;
		const Texture& stage = ResourceManager::textures[index];
		if (!isArrayIndexInBounds(SPR_SEKAI_STAGE, stage.sprites)) return;
		const Sprite& stageSprite = stage.sprites[SPR_SEKAI_STAGE];
		constexpr float stageWidth = (Engine::STAGE_TEX_WIDTH / Engine::STAGE_LANE_WIDTH) * Engine::STAGE_NUM_LANES;
		constexpr float stageLeft  = -stageWidth / 2;
		constexpr float stageTop = Engine::STAGE_LANE_TOP / Engine::STAGE_LANE_HEIGHT;
		constexpr float stageHeight = Engine::STAGE_TEX_HEIGHT / Engine::STAGE_LANE_HEIGHT;
		
		renderer->drawRectangle(Vector2(stageLeft, stageTop), Vector2(stageWidth, stageHeight), stage, stageSprite.getX(), stageSprite.getX() + stageSprite.getWidth(), stageSprite.getY(), stageSprite.getY() + stageSprite.getHeight(), Color(defaultTint.r, defaultTint.g, defaultTint.b, defaultTint.a * config.pvStageOpacity), -1);
	}

	void ScorePreviewWindow::drawStageCoverMask(Renderer* renderer)
	{
		int index = ResourceManager::getTexture("stage");
		if (index == -1) return;
		const Texture& stage = ResourceManager::textures[index];
		constexpr float stageWidth = (Engine::STAGE_TEX_WIDTH / Engine::STAGE_LANE_WIDTH) * Engine::STAGE_NUM_LANES;
		constexpr float stageLeft = -stageWidth / 2, stageRight = stageWidth / 2;

		constexpr float stageTop = Engine::STAGE_LANE_TOP / Engine::STAGE_LANE_HEIGHT;
		const float stageHeight = config.pvStageCover * (1 - stageTop);

		static auto model = DirectX::XMMatrixTranslation(0, 0, 1);
		auto vPos = Engine::quadvPos(stageLeft, stageRight, stageTop + stageHeight, 0);
		auto uv = Utils::getUV(0.f, 1.f, 0.f, 1.f);
		renderer->pushQuad(vPos, uv, model, toFloat4(defaultTint, 0.f), (int)stage.getID(), 0);
	}

	void ScorePreviewWindow::drawStageCover(Renderer* renderer)
	{
		int index = ResourceManager::getTexture("stage");
		if (index == -1) return;
		const Texture& stage = ResourceManager::textures[index];
		if (!isArrayIndexInBounds(SPR_SEKAI_STAGE, stage.sprites)) return;
		const Sprite& stageSprite = stage.sprites[SPR_SEKAI_STAGE];
		constexpr float stageWidth = (Engine::STAGE_TEX_WIDTH / Engine::STAGE_LANE_WIDTH) * Engine::STAGE_NUM_LANES;
		const float stageLeft = -stageWidth / 2, stageRight = stageWidth / 2;

		constexpr float stageTop = Engine::STAGE_LANE_TOP / Engine::STAGE_LANE_HEIGHT;
		const float stageHeight = config.pvStageCover * (1 - stageTop);
		const float spriteHeight = config.pvStageCover * (Engine::STAGE_LANE_HEIGHT - Engine::STAGE_LANE_TOP);

		static auto model = DirectX::XMMatrixTranslation(0, 0, 1);
		
		float texW = (float)stage.getWidth();
		float texH = (float)stage.getHeight();
		auto vPos = Engine::quadvPos(stageLeft, stageRight, stageTop + stageHeight, stageTop);
		auto uv = Utils::getUV(stageSprite.getX() / texW, (stageSprite.getX() + stageSprite.getWidth()) / texW, stageSprite.getY() / texH, (stageSprite.getY() + spriteHeight) / texH);

		renderer->pushQuad(vPos, uv, model, DirectX::XMFLOAT4(0, 0, 0, config.pvStageOpacity), (int)stage.getID(), 0);
	}

	void ScorePreviewWindow::drawStageCoverDecoration(Renderer *renderer) {}

	void ScorePreviewWindow::drawNotes(const ScoreContext& context, Renderer *renderer)
	{
		double current_tm = accumulateDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges);
		double scaled_tm = Engine::accumulateScaledDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges, context.score.hiSpeedChanges);
		const auto& drawData = context.scorePreviewDrawData;
	
		for (auto& note : drawData.drawingNotes)
		{
			if (scaled_tm < note.visualTime.min || scaled_tm > note.visualTime.max)
				continue;
			const Note& noteData = context.score.notes.at(note.refID);
			double y = Engine::approach(note.visualTime.min, note.visualTime.max, scaled_tm);
			float l = Engine::laneToLeft(noteData.lane), r = Engine::laneToLeft(noteData.lane) + noteData.width;
			drawNoteBase(renderer, noteData, l, r, (float)y);
			if (noteData.friction)
				drawTraceDiamond(renderer, noteData, l, r, (float)y);
			if (noteData.isFlick()) 
				drawFlickArrow(renderer, noteData, (float)y, current_tm);
		}
	}

	void ScorePreviewWindow::drawLines(const ScoreContext& context, Renderer* renderer)
	{
		if (!config.pvSimultaneousLine || noteTextures.notes == -1) return;
		double scaled_tm = Engine::accumulateScaledDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges, context.score.hiSpeedChanges);
		const auto& drawData = context.scorePreviewDrawData.drawingLines;

		const Texture& texture = getNoteTexture();
		size_t sprIndex = SPR_SIMULTANEOUS_CONNECTION;
		if (!isArrayIndexInBounds(sprIndex, texture.sprites)) return;
		const Sprite& sprite = texture.sprites[sprIndex];

		size_t transIndex = static_cast<size_t>(SpriteType::SimultaneousLine);
		if (!isArrayIndexInBounds(transIndex, ResourceManager::spriteTransforms)) return;
		const SpriteTransform& lineTransform = ResourceManager::spriteTransforms[transIndex];

		const float noteTop = 1. + Engine::getNoteHeight(), noteBottom = 1. - Engine::getNoteHeight();
		float texW = (float)texture.getWidth();
		float texH = (float)texture.getHeight();

		for (auto& line : drawData)
		{
			if (scaled_tm < line.visualTime.min || scaled_tm > line.visualTime.max) continue;
			float noteLeft = line.xPos.min, noteRight = line.xPos.max;
			if (config.pvMirrorScore) std::swap(noteLeft *= -1, noteRight *= -1);
			float y = (float)Engine::approach(line.visualTime.min, line.visualTime.max, scaled_tm);
			
			auto vPos = lineTransform.apply(Engine::perspectiveQuadvPos(noteLeft, noteRight, noteTop, noteBottom));
			auto uv = Utils::getUV(sprite.getX() / texW, (sprite.getX() + sprite.getWidth()) / texW, sprite.getY() / texH, (sprite.getY() + sprite.getHeight()) / texH);
			
			renderer->pushQuad(vPos, uv, DirectX::XMMatrixScaling(y, y, 1.f), toFloat4(defaultTint), (int)texture.getID(), Engine::getZIndex(SpriteLayer::UNDER_NOTE_EFFECT, 0, y));
		}
	}

	void ScorePreviewWindow::drawHoldTicks(const ScoreContext &context, Renderer *renderer)
	{
		if (noteTextures.notes == -1) return;
		double scaled_tm = Engine::accumulateScaledDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges, context.score.hiSpeedChanges);
		const float notesHeight = Engine::getNoteHeight() * 1.3f;
		const float w = notesHeight / scaledAspectRatio;
		const float noteTop = 1. + notesHeight, noteBottom = 1. - notesHeight;
		const Texture& texture = getNoteTexture();
		float texW = (float)texture.getWidth();
		float texH = (float)texture.getHeight();

		size_t transIndex = static_cast<size_t>(SpriteType::HoldTick);
		if (!isArrayIndexInBounds(transIndex, ResourceManager::spriteTransforms)) return;
		const SpriteTransform& transform = ResourceManager::spriteTransforms[transIndex];

		for (auto& tick : context.scorePreviewDrawData.drawingHoldTicks)
		{
			if (scaled_tm < tick.visualTime.min || scaled_tm > tick.visualTime.max) continue;
			int sprIndex = getNoteSpriteIndex(context.score.notes.at(tick.refID));
			if (!isArrayIndexInBounds(sprIndex, texture.sprites)) continue;
			const Sprite& sprite = texture.sprites[sprIndex];
			float y = (float)Engine::approach(tick.visualTime.min, tick.visualTime.max, scaled_tm);
			const float tickCenter = tick.center * (config.pvMirrorScore ? -1 : 1);
			
			auto vPos = transform.apply(Engine::quadvPos(tickCenter - w, tickCenter + w, noteTop, noteBottom));
			auto uv = Utils::getUV(sprite.getX() / texW, (sprite.getX() + sprite.getWidth()) / texW, sprite.getY() / texH, (sprite.getY() + sprite.getHeight()) / texH);
			
			renderer->pushQuad(vPos, uv, DirectX::XMMatrixScaling(y, y, 1.f), toFloat4(defaultTint), (int)texture.getID(), Engine::getZIndex(SpriteLayer::DIAMOND, tickCenter, y));
		}
	}

	void ScorePreviewWindow::drawHoldCurves(const ScoreContext& context, Renderer* renderer)
	{
		const float total_tm = accumulateDuration(context.scorePreviewDrawData.maxTicks, TICKS_PER_BEAT, context.score.tempoChanges);
		const double current_tm = accumulateDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges);
		const double current_stm = Engine::accumulateScaledDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges, context.score.hiSpeedChanges);
		const float noteDuration = Engine::getNoteDuration(config.pvNoteSpeed);
		const double visible_stm = current_stm + noteDuration;
		const float mirror = config.pvMirrorScore ? -1 : 1;
		const auto& drawData = context.scorePreviewDrawData;

		for (auto& segment : drawData.drawingHoldSegments)
		{
			if ((std::min(segment.headTime, segment.tailTime) > visible_stm && segment.startTime > current_tm) || current_tm >= segment.endTime) continue;

			const Note& holdEnd = context.score.notes.at(segment.endID);
			const Note& holdStart = context.score.notes.at(holdEnd.parentID);
			float holdStartCenter = Engine::getNoteCenter(holdStart) * mirror;
			bool isHoldActivated = current_tm >= segment.activeTime;
			bool isSegmentActivated = current_tm >= segment.startTime;

			int textureID = segment.isGuide ? noteTextures.touchLine : noteTextures.holdPath;
			if (textureID == -1) continue;
			const Texture& texture = ResourceManager::textures[textureID];
			const int sprIndex = (!holdStart.critical ? 1 : 3);
			if (!isArrayIndexInBounds(sprIndex, texture.sprites)) continue;
			const Sprite& segmentSprite = texture.sprites[sprIndex];

			double segmentHead_stm = std::min(segment.headTime, segment.tailTime);
			double segmentTail_stm = std::max(segment.headTime, segment.tailTime);
			double segmentStart_stm = std::max(segmentHead_stm, current_stm);
			double segmentEnd_stm = std::min(segmentTail_stm, visible_stm);
			double segmentStartProgress, segmentEndProgress, holdStartProgress, holdEndProgress;

			if (!isSegmentActivated)
			{
				segmentStartProgress = 0;
				segmentEndProgress = unlerpD(segmentHead_stm, segmentTail_stm, segmentEnd_stm);
			}
			else
			{
				segmentStartProgress = unlerpD(segment.startTime, segment.endTime, current_tm);
				segmentEndProgress = lerpD(segmentStartProgress, 1.0, unlerpD(current_stm, segmentTail_stm, segmentEnd_stm));
			}

			const int steps = (segment.ease == EaseType::Linear ? 10 : 15) + static_cast<int>(std::log(std::max((segmentEnd_stm - segmentStart_stm) / noteDuration, 4.5399e-5)) + 0.5);
			const auto ease = getEaseFunction(segment.ease);
			float startLeft = segment.headLeft, startRight = segment.headRight, endLeft = segment.tailLeft, endRight = segment.tailRight;

			if (isSegmentActivated && context.score.holdNotes.at(holdStart.ID).startType == HoldNoteType::Normal)
			{
				float l = ease(startLeft, endLeft, (float)segmentStartProgress), r = ease(startRight, endRight, (float)segmentStartProgress);
				drawNoteBase(renderer, holdStart, l, r, 1.f, segment.activeTime / total_tm);
				if (holdStart.friction) drawTraceDiamond(renderer, holdStart, l, r, 1.f);
			}

			if (config.pvMirrorScore)
			{
				std::swap(startLeft *= -1, startRight *= -1);
				std::swap(endLeft *= -1, endRight *= -1);
			}

			if (segment.isGuide)
			{
				const HoldNote& hold = context.score.holdNotes.at(holdStart.ID);
				double totalJoints = 1 + hold.steps.size();
				double headProgress = segment.tailStepIndex / totalJoints;
				double tailProgress = (segment.tailStepIndex + 1) / totalJoints;

				if (!isSegmentActivated)
				{
					holdStartProgress = headProgress;
					holdEndProgress = lerpD(headProgress, tailProgress, unlerpD(segmentHead_stm, segmentTail_stm, segmentEnd_stm));
				}
				else
				{
					holdStartProgress = lerpD(headProgress, tailProgress, unlerp(segment.startTime, segment.endTime, current_tm));
					holdEndProgress = lerpD(holdStartProgress, tailProgress, unlerpD(current_stm, segment.tailTime, segmentEnd_stm));
				}
			}

			double from_percentage = 0;
			double stepStart_stm = segmentStart_stm;
			double stepTop = Engine::approach(stepStart_stm - noteDuration, stepStart_stm, current_stm);
			double stepStartProgress = segmentStartProgress;

			auto model = DirectX::XMMatrixIdentity();
			float alpha = segment.isGuide ? config.pvGuideAlpha : config.pvHoldAlpha;
			int zIndex = Engine::getZIndex(segment.isGuide ? SpriteLayer::GUIDE_PATH : SpriteLayer::HOLD_PATH, holdStartCenter, segment.activeTime / total_tm);

			for (int i = 0; i < steps; i++)
			{
				double to_percentage = double(i + 1) / steps;
				double stepEnd_stm = lerpD(segmentStart_stm, segmentEnd_stm, to_percentage);
				double stepBottom = Engine::approach(stepEnd_stm - noteDuration, stepEnd_stm, current_stm);
				double stepEndProgress = lerpD(segmentStartProgress, segmentEndProgress, to_percentage);

				float stepStartLeft = ease(startLeft, endLeft, (float)stepStartProgress);
				float   stepEndLeft = ease(startLeft, endLeft, (float)stepEndProgress);
				float stepStartRight = ease(startRight, endRight, (float)stepStartProgress);
				float   stepEndRight = ease(startRight, endRight, (float)stepEndProgress);

				auto vPos = Engine::perspectiveQuadvPos(stepStartLeft, stepEndLeft, stepStartRight, stepEndRight, (float)stepTop, (float)stepBottom);

				float spr_x1, spr_x2, spr_y1, spr_y2;
				if (segment.isGuide)
				{
					spr_x1 = segmentSprite.getX() + GUIDE_XCUTOFF;
					spr_x2 = segmentSprite.getX() + segmentSprite.getWidth() - GUIDE_XCUTOFF;
					spr_y1 = lerp(segmentSprite.getY() + segmentSprite.getHeight() - GUIDE_Y_BOTTOM_CUTOFF, segmentSprite.getY() + GUIDE_Y_TOP_CUTOFF, (float)lerpD(holdStartProgress, holdEndProgress, from_percentage));
					spr_y2 = lerp(segmentSprite.getY() + segmentSprite.getHeight() - GUIDE_Y_BOTTOM_CUTOFF, segmentSprite.getY() + GUIDE_Y_TOP_CUTOFF, (float)lerpD(holdStartProgress, holdEndProgress, to_percentage));
				}
				else
				{
					spr_x1 = segmentSprite.getX() + HOLD_XCUTOFF;
					spr_x2 = segmentSprite.getX() + segmentSprite.getWidth() - HOLD_XCUTOFF;
					spr_y1 = segmentSprite.getY();
					spr_y2 = segmentSprite.getY() + segmentSprite.getHeight();
				}

				float texW = (float)texture.getWidth();
				float texH = (float)texture.getHeight();
				auto uv = Utils::getUV(spr_x1 / texW, spr_x2 / texW, spr_y1 / texH, spr_y2 / texH);

				if (config.pvHoldAnimation && isHoldActivated && isArrayIndexInBounds(sprIndex - 1, texture.sprites))
				{
					const Sprite& activeSprite = texture.sprites[sprIndex - 1];
					const int norm2ActiveOffset = (int)(activeSprite.getY() - segmentSprite.getY());
					double delta_tm = current_tm - segment.activeTime;
					float normalAplha = (std::cos((float)delta_tm * MATH_PI * 2.f) + 2.f) / 3.f;

					renderer->pushQuad(vPos, uv, model, toFloat4(defaultTint, alpha * normalAplha), (int)texture.getID(), zIndex);
					auto uvActive = Utils::getUV(spr_x1 / texW, spr_x2 / texW, (spr_y1 + norm2ActiveOffset) / texH, (spr_y2 + norm2ActiveOffset) / texH);
					renderer->pushQuad(vPos, uvActive, model, toFloat4(defaultTint, alpha * (1.f - normalAplha)), (int)texture.getID(), zIndex);
				}
				else
					renderer->pushQuad(vPos, uv, model, toFloat4(defaultTint, alpha), (int)texture.getID(), zIndex);

				from_percentage = to_percentage;
				stepStart_stm = stepEnd_stm;
				stepTop = stepBottom;
				stepStartProgress = stepEndProgress;
			}
		}
	}

	void ScorePreviewWindow::drawNoteBase(Renderer* renderer, const Note& note, float noteLeft, float noteRight, float y, float zScalar)
	{
		if (noteTextures.notes == -1) return;
		const Texture& texture = getNoteTexture();
		const int sprIndex = getNoteSpriteIndex(note);
		if (!isArrayIndexInBounds(sprIndex, texture.sprites)) return;
		const Sprite& sprite = texture.sprites[sprIndex];

		size_t transIndexM = static_cast<size_t>(SpriteType::NoteMiddle);
		size_t transIndexL = static_cast<size_t>(SpriteType::NoteLeft);
		size_t transIndexR = static_cast<size_t>(SpriteType::NoteRight);
		if (!isArrayIndexInBounds(transIndexM, ResourceManager::spriteTransforms) ||
			!isArrayIndexInBounds(transIndexL, ResourceManager::spriteTransforms) ||
			!isArrayIndexInBounds(transIndexR, ResourceManager::spriteTransforms)) return;

		const SpriteTransform& mTransform = ResourceManager::spriteTransforms[transIndexM];
		const SpriteTransform& lTransform = ResourceManager::spriteTransforms[transIndexL];
		const SpriteTransform& rTransform = ResourceManager::spriteTransforms[transIndexR];

		const float noteHeight = Engine::getNoteHeight();
		const float noteTop = 1.f - noteHeight, noteBottom = 1.f + noteHeight;
		if (config.pvMirrorScore) std::swap(noteLeft *= -1.f, noteRight *= -1.f);
		int zIndex = Engine::getZIndex(!note.friction ? SpriteLayer::BASE_NOTE : SpriteLayer::TICK_NOTE, noteLeft + (noteRight - noteLeft) / 2.f, y * zScalar);

		auto model = DirectX::XMMatrixScaling(y, y, 1.f);
		float texW = (float)texture.getWidth();
		float texH = (float)texture.getHeight();
		std::array<DirectX::XMFLOAT4, 4> vPos, uv;

		// Middle
		vPos = mTransform.apply(Engine::perspectiveQuadvPos(noteLeft + 0.25f, noteRight - 0.3f, noteTop, noteBottom));
		uv = Utils::getUV((sprite.getX() + NOTE_SIDE_WIDTH) / texW, (sprite.getX() + sprite.getWidth() - NOTE_SIDE_WIDTH) / texW, sprite.getY() / texH, (sprite.getY() + sprite.getHeight()) / texH);
		renderer->pushQuad(vPos, uv, model, toFloat4(defaultTint), (int)texture.getID(), zIndex);

		// Left slice
		vPos = lTransform.apply(Engine::perspectiveQuadvPos(noteLeft, noteLeft + 0.25f, noteTop, noteBottom));
		uv = Utils::getUV((sprite.getX() + NOTE_SIDE_PAD) / texW, (sprite.getX() + NOTE_SIDE_WIDTH) / texW, sprite.getY() / texH, (sprite.getY() + sprite.getHeight()) / texH);
		renderer->pushQuad(vPos, uv, model, toFloat4(defaultTint), (int)texture.getID(), zIndex);
		
		// Right slice
		vPos = rTransform.apply(Engine::perspectiveQuadvPos(noteRight - 0.3f, noteRight, noteTop, noteBottom));
		uv = Utils::getUV((sprite.getX() + sprite.getWidth() - NOTE_SIDE_WIDTH) / texW, (sprite.getX() + sprite.getWidth() - NOTE_SIDE_PAD) / texW, sprite.getY() / texH, (sprite.getY() + sprite.getHeight()) / texH);
		renderer->pushQuad(vPos, uv, model, toFloat4(defaultTint), (int)texture.getID(), zIndex);
	}

	void ScorePreviewWindow::drawTraceDiamond(Renderer *renderer, const Note &note, float noteLeft, float noteRight, float y)
	{
		if (noteTextures.notes == -1) return;
		const Texture& texture = getNoteTexture();
		int frictionSprIndex = getFrictionSpriteIndex(note);
		if (!isArrayIndexInBounds(frictionSprIndex, texture.sprites)) return;
		const Sprite& frictionSpr = texture.sprites[frictionSprIndex];

		size_t transIndex = static_cast<size_t>(SpriteType::TraceDiamond);
		if (!isArrayIndexInBounds(transIndex, ResourceManager::spriteTransforms)) return;
		const SpriteTransform& transform = ResourceManager::spriteTransforms[transIndex];

		const float w = Engine::getNoteHeight() / scaledAspectRatio;
		const float noteTop = 1.f + Engine::getNoteHeight(), noteBottom = 1.f - Engine::getNoteHeight();
		if (config.pvMirrorScore) std::swap(noteLeft *= -1.f, noteRight *= -1.f);
		const float noteCenter = noteLeft + (noteRight - noteLeft) / 2.f;
		int zIndex = Engine::getZIndex(SpriteLayer::DIAMOND, noteCenter, y);

		auto vPos = transform.apply(Engine::quadvPos(noteCenter - w, noteCenter + w, noteTop, noteBottom));
		
		float texW = (float)texture.getWidth();
		float texH = (float)texture.getHeight();
		auto uv = Utils::getUV(frictionSpr.getX() / texW, (frictionSpr.getX() + frictionSpr.getWidth()) / texW, frictionSpr.getY() / texH, (frictionSpr.getY() + frictionSpr.getHeight()) / texH);
		
		auto model = DirectX::XMMatrixScaling(y, y, 1.f);
		renderer->pushQuad(vPos, uv, model, toFloat4(defaultTint), (int)texture.getID(), zIndex);
	}

	void ScorePreviewWindow::drawFlickArrow(Renderer *renderer, const Note &note, float y, double time)
	{
		if (noteTextures.notes == -1) return;
		const Texture& texture = getNoteTexture();
		const int sprIndex = getFlickArrowSpriteIndex(note);
		if (!isArrayIndexInBounds(sprIndex, texture.sprites)) return;
		const Sprite& arrowSprite = texture.sprites[sprIndex];

		size_t flickTransformIdx = std::clamp((int)note.width, 1, MAX_FLICK_SPRITES) - 1 + static_cast<int>((note.flick == FlickType::Left || note.flick == FlickType::Right) ? SpriteType::FlickArrowLeft : SpriteType::FlickArrowUp);
		if (!isArrayIndexInBounds(flickTransformIdx, ResourceManager::spriteTransforms)) return;
		const SpriteTransform& transform = ResourceManager::spriteTransforms[flickTransformIdx];

		const int mirror = config.pvMirrorScore ? -1 : 1;
		const int flickDirection = mirror * (note.flick == FlickType::Left ? -1 : (note.flick == FlickType::Right ? 1 : 0));
		const float center = Engine::getNoteCenter(note) * mirror;
		const float w = std::clamp((int)note.width, 1, MAX_FLICK_SPRITES) * (note.flick == FlickType::Right ? -1.f : 1.f) * mirror / 4.f;
		
		auto vPos = transform.apply(Engine::quadvPos(center - w, center + w, 1.f, 1.f - 2.f * std::abs(w) * scaledAspectRatio));
		
		float texW = (float)texture.getWidth();
		float texH = (float)texture.getHeight();
		auto uv = Utils::getUV(arrowSprite.getX() / texW, (arrowSprite.getX() + arrowSprite.getWidth()) / texW, arrowSprite.getY() / texH, (arrowSprite.getY() + arrowSprite.getHeight()) / texH);
		
		int zIndex = Engine::getZIndex(SpriteLayer::FLICK_ARROW, center, y);
		
		if (config.pvFlickAnimation)
		{
			double t = std::fmod(time, 0.5) / 0.5;
			auto cubicEaseIn = [](double val) { return (float)(val * val * val); };
			auto animationVector = DirectX::XMVectorScale(DirectX::XMVectorSet((float)flickDirection, -2.f * scaledAspectRatio, 0.f, 0.f), (float)t);
			auto model = DirectX::XMMatrixMultiply(DirectX::XMMatrixTranslationFromVector(animationVector), DirectX::XMMatrixScaling(y, y, 1.f));
			renderer->pushQuad(vPos, uv, model, toFloat4(defaultTint, 1.f - cubicEaseIn(t)), (int)texture.getID(), zIndex);
		}
		else
		{
			auto model = DirectX::XMMatrixScaling(y, y, 1.f);
			renderer->pushQuad(vPos, uv, model, toFloat4(defaultTint), (int)texture.getID(), zIndex);
		}
	}

	void ScorePreviewWindow::updateToolbar(ScoreEditorTimeline &timeline, ScoreContext &context) const
	{
		static float lastHoveredTime = -1;
		constexpr float MAX_NO_HOVER_TIME = 1.5f;
		static float toolBarWidth = UI::btnNormal.x * 2;
		if (!config.pvDrawToolbar) return;
		ImGuiIO io = ImGui::GetIO();
		ImGui::SetNextWindowPos(ImGui::GetWindowPos() + ImVec2{
			ImGui::GetContentRegionAvail().x - ImGui::GetStyle().WindowPadding.x * 4 - toolBarWidth,
			ImGui::GetStyle().WindowPadding.y * 5
		});
		ImGui::SetNextWindowSizeConstraints({48, 0}, {120, FLT_MAX}, NULL);
		auto easeInCubic = [](float t) { return t * t * t; };
		float childBgAlpha = std::clamp(easeInCubic(unlerp(MAX_NO_HOVER_TIME, 0.f, lastHoveredTime)), 0.25f, 1.f);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.f);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_WindowBg, childBgAlpha));
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });
		
		ImGui::Begin("###preview_toolbar", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_ChildWindow);
		toolBarWidth = ImGui::GetWindowWidth();
		float centeredXBtn = toolBarWidth / 2 - UI::btnNormal.x / 2;
		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) lastHoveredTime = 0;
		else lastHoveredTime = std::min(io.DeltaTime + lastHoveredTime, MAX_NO_HOVER_TIME);

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(ICON_FA_ANGLE_DOUBLE_UP, UI::btnNormal, true, context.currentTick < context.scorePreviewDrawData.maxTicks + TICKS_PER_BEAT))
		{
			if (timeline.isPlaying()) timeline.setPlaying(context, false);
			context.currentTick = timeline.roundTickDown(context.currentTick, timeline.getDivision()) + (TICKS_PER_BEAT / (timeline.getDivision() / 4));
		}

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(ICON_FA_ANGLE_UP, UI::btnNormal, true, context.currentTick < context.scorePreviewDrawData.maxTicks + TICKS_PER_BEAT))
		{
			if (timeline.isPlaying()) timeline.setPlaying(context, false);
			context.currentTick++;
		}

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(ICON_FA_STOP, UI::btnNormal, false)) timeline.stop(context);

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(timeline.isPlaying() ? ICON_FA_PAUSE : ICON_FA_PLAY, UI::btnNormal)) timeline.setPlaying(context, !timeline.isPlaying());

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(ICON_FA_ANGLE_DOWN, UI::btnNormal, true, context.currentTick > 0))
		{
			if (timeline.isPlaying()) timeline.setPlaying(context, false);
			context.currentTick--;
		}

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(ICON_FA_ANGLE_DOUBLE_DOWN, UI::btnNormal, true, context.currentTick > 0))
		{
			if (timeline.isPlaying()) timeline.setPlaying(context, false);
			context.currentTick = std::max(timeline.roundTickDown(context.currentTick, timeline.getDivision()) - (TICKS_PER_BEAT / (timeline.getDivision() / 4)), 0);
		}

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(isFullWindow() ? ICON_FA_COMPRESS : ICON_FA_EXPAND)) fullWindow = !isFullWindow();

		ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal);

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(ICON_FA_MINUS, UI::btnNormal, false, timeline.getPlaybackSpeed() > 0.25f)) timeline.setPlaybackSpeed(context, timeline.getPlaybackSpeed() - 0.25f);

		const float playbackStrWidth = ImGui::CalcTextSize("0000%").x;
		ImGui::SetCursorPosX(toolBarWidth / 2 - playbackStrWidth / 2);
		UI::transparentButton(IO::formatString("%.0f%%", timeline.getPlaybackSpeed() * 100).c_str(), ImVec2{playbackStrWidth, UI::btnNormal.y }, false, false);

		ImGui::SetCursorPosX(centeredXBtn);
		if (UI::transparentButton(ICON_FA_PLUS, UI::btnNormal, false, timeline.getPlaybackSpeed() < 1.0f)) timeline.setPlaybackSpeed(context, timeline.getPlaybackSpeed() + 0.25f);

		ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal);

		float currentTm = accumulateDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges);
		double currentScaledTm = Engine::accumulateScaledDuration(context.currentTick, TICKS_PER_BEAT, context.score.tempoChanges, context.score.hiSpeedChanges);
		int currentMeasure = accumulateMeasures(context.currentTick, TICKS_PER_BEAT, context.score.timeSignatures);
		const TimeSignature& ts = context.score.timeSignatures[findTimeSignature(currentMeasure, context.score.timeSignatures)];
		const Tempo& tempo = getTempoAt(context.currentTick, context.score.tempoChanges);
		int hiSpeedIdx = findHighSpeedChange(context.currentTick, context.score.hiSpeedChanges, 0);
		float speed = (hiSpeedIdx == -1 ? 1.0f : context.score.hiSpeedChanges[hiSpeedIdx].speed);

		char rhythmString[256];
		snprintf(rhythmString, sizeof(rhythmString), "%02d:%02d:%02d|%.2fs|%d/%d|%g BPM|%sx",
			static_cast<int>(currentTm / 60), static_cast<int>(std::fmod(currentTm, 60.f)), static_cast<int>(std::fmod(currentTm * 100, 100.f)),
			currentScaledTm,
			ts.numerator, ts.denominator,
			tempo.bpm,
			IO::formatFixedFloatTrimmed(speed).c_str()
		);
		char* str = strtok(rhythmString, "|");
		ImGui::SetCursorPosX(toolBarWidth / 2 - ImGui::CalcTextSize(str).x / 2);
		ImGui::Text(str);
		for (auto&& col : {feverColor, timeColor, tempoColor, speedColor})
		{
			str = strtok(NULL, "|");
			ImGui::SetCursorPosX(toolBarWidth / 2 - ImGui::CalcTextSize(str).x / 2);
			ImGui::TextColored(ImColor(col), str);
		}
		ImGui::EndChild();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar();
	}

	float ScorePreviewWindow::getScrollbarWidth() const { return ImGui::GetStyle().ScrollbarSize + 4.f; }

	void ScorePreviewWindow::updateScrollbar(ScoreEditorTimeline &timeline, ScoreContext &context) const
	{
		constexpr float scrollpadY = 30.f;
		ImGuiIO& io = ImGui::GetIO();
		ImGuiStyle& style = ImGui::GetStyle();
		ImVec2 contentSize = ImGui::GetWindowContentRegionMax();
		ImVec2 cursorBegPos = ImGui::GetCursorStartPos();
		ImVec2 scrollbarSize = { getScrollbarWidth(), contentSize.y - cursorBegPos.y };
		
		ImGui::SetCursorPos(cursorBegPos + ImVec2{ contentSize.x - scrollbarSize.x - style.WindowPadding.x / 2, 0 });
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarBg));
		ImGui::BeginChild("###scrollbar", scrollbarSize, false, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
		ImGui::PopStyleColor();

		ImVec2 scrollContentSize = ImGui::GetContentRegionAvail();
		ImVec2 scrollMaxSize = ImGui::GetWindowContentRegionMax();
		int maxTicks = std::max(context.scorePreviewDrawData.maxTicks, 1);

		float scrollRatio = std::min((float)(Engine::getNoteDuration(config.pvNoteSpeed) / accumulateDuration(context.scorePreviewDrawData.maxTicks, TICKS_PER_BEAT, context.score.tempoChanges)), 1.f);
		
		float progress = 1.f - std::min(float(context.currentTick) / maxTicks, 1.f);
		float handleHeight = std::max(20.f, scrollContentSize.y * scrollRatio);
		
		bool scrollbarActive = false;
		ImGui::BeginDisabled(timeline.isPlaying());
		ImGui::SetCursorPos(ImGui::GetCursorStartPos());
		ImGui::InvisibleButton("##scroll_bg", contentSize, ImGuiButtonFlags_NoNavFocus);
		scrollbarActive |= ImGui::IsItemActive();

		ImVec2 handleSize = {style.ScrollbarSize, handleHeight};
		ImVec2 handlePos = {scrollMaxSize.x / 2 - handleSize.x / 2, lerp(0.f, scrollMaxSize.y - handleHeight, progress)};
		ImVec2 absHandlePos = ImGui::GetWindowPos() + handlePos;

		ImGui::SetCursorPos(handlePos);
		ImGui::InvisibleButton("##scroll_handle", handleSize);
		scrollbarActive |= ImGui::IsItemActive();

		ImGuiCol_ handleColBase = scrollbarActive ? ImGuiCol_ScrollbarGrabActive : ImGui::IsItemHovered() ? ImGuiCol_ScrollbarGrabHovered : ImGuiCol_ScrollbarGrab;
		
		ImGui::RenderFrame(absHandlePos, absHandlePos + ImGui::GetItemRectSize(), ImGui::GetColorU32(handleColBase), true, 3.f);
		ImGui::EndDisabled();

		if (scrollbarActive)
		{
			float absScrollStart = ImGui::GetWindowPos().y + handleSize.y / 2.f;
			float absScrollEnd = ImGui::GetWindowPos().y + scrollMaxSize.y - handleSize.y / 2.f;
			float mouseProgress = 1.f - std::clamp(unlerp(absScrollStart, absScrollEnd, io.MousePos.y), 0.f, 1.f);
			context.currentTick = (int)std::round(lerp(0.f, (float)maxTicks, mouseProgress));	
		}
		ImGui::EndChild();
	}
}