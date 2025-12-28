#include "SonolusSerializer.h"
#include "Constants.h"
#include "IO.h"
#include "File.h"
#include "Application.h"
#include "ApplicationConfiguration.h"
#include "Colors.h"

#ifdef _DEBUG
#define PRINT_DEBUG(...)                                                                           \
	do                                                                                             \
	{                                                                                              \
		fprintf(stderr, __VA_ARGS__);                                                              \
		fprintf(stderr, "\n");                                                                     \
	} while (0)
#else
#define PRINT_DEBUG(...)                                                                           \
	do                                                                                             \
	{                                                                                              \
	} while (0)
#endif // _DEBUG

using json = nlohmann::json;
using namespace Sonolus;

namespace MikuMikuWorld
{
	constexpr int UNSUPPORTED_ENUM = 2;

	void SonolusSerializer::serialize(const Score& score, std::string filename)
	{
		LevelData levelData = engine->serialize(score);
		std::string serializedData = json(levelData).dump(prettyDump ? 2 : -1);
		std::vector<uint8_t> serializedBytes(serializedData.begin(), serializedData.end());
		if (compressData)
			serializedBytes = IO::deflateGzip(serializedBytes);

		IO::File levelFile(filename, IO::FileMode::WriteBinary);
		levelFile.writeAllBytes(serializedBytes);
		levelFile.flush();
		levelFile.close();
	}

	Score SonolusSerializer::deserialize(std::string filename)
	{
		if (!IO::File::exists(filename.c_str()))
			return {};
		IO::File levelFile(filename, IO::FileMode::ReadBinary);
		std::vector<uint8_t> bytes = levelFile.readAllBytes();
		levelFile.close();
		if (IO::isGzipCompressed(bytes))
			bytes = IO::inflateGzip(bytes);
		json levelDataJson = json::parse(std::string(bytes.begin(), bytes.end()));
		LevelData levelData;
		levelDataJson.get_to(levelData);

		return engine->deserialize(levelData);
	}

#pragma region HelperFunctions

	class IdManager
	{
		inline int64_t getID(size_t idx)
		{
			auto it = indexToID.find(idx);
			if (it == indexToID.end())
				it = indexToID.emplace_hint(indexToID.end(), idx, nextID++);
			return it->second;
		}
		inline bool hasIdx(size_t idx) const
		{
			auto it = indexToID.find(idx);
			return it != indexToID.end();
		}
		inline static std::string toRef(int64_t index, int64_t base)
		{
			// Basically base 10 to base n
			if (index < 0)
				return toRef(-index, base).insert(0, "-");
			std::string ref;
			lldiv_t dv;
			do
			{
				dv = std::div(static_cast<long long>(index), static_cast<long long>(base));
				ref += dv.rem < 10 ? ('0' + dv.rem) : ('a' + (dv.rem - 10));
				index = dv.quot;
			} while (dv.quot);
			return { ref.rbegin(), ref.rend() };
		}

	  public:
		IdManager(int64_t base = 36ll, int64_t nextID = 0) : nextID(nextID), base(base), indexToID()
		{
		}

		inline void clear() { indexToID.clear(); }
		inline std::string getStartRef() { return toRef(getID(START_INDEX), base); }
		inline std::string getEndRef() { return toRef(getID(END_INDEX), base); }
		inline std::string getRef(size_t index) { return toRef(getID(index), base); }
		inline std::string getExistingRef(size_t index) const
		{
			return hasIdx(index) ? toRef(indexToID.at(index), base) : "";
		}
		inline std::string getNextRef() { return toRef(nextID++, base); }

	  private:
		int64_t nextID, base;
		std::map<size_t, int64_t> indexToID;
		static constexpr size_t START_INDEX = size_t(-1);
		static constexpr size_t END_INDEX = size_t(-2);
	};

	double SonolusEngine::toBgmOffset(float musicOffset)
	{
		return musicOffset == 0 ? 0.0 : roundOff(-musicOffset / 1000.0);
	}

	LevelDataEntity SonolusEngine::toBpmChangeEntity(const Tempo& tempo)
	{
		return { "#BPM_CHANGE", { { "#BEAT", ticksToBeats(tempo.tick) }, { "#BPM", tempo.bpm } } };
	}

	SonolusEngine::RealType SonolusEngine::ticksToBeats(TickType ticks, TickType beatTicks)
	{
		return roundOff(static_cast<RealType>(ticks) / beatTicks);
	}

	SonolusEngine::RealType SonolusEngine::widthToSize(WidthType width)
	{
		return static_cast<RealType>(width) / 2;
	}

	SonolusEngine::RealType SonolusEngine::toSonolusLane(LaneType lane, WidthType width)
	{
		return (lane - 6) + (static_cast<RealType>(width) / 2);
	}

	float SonolusEngine::fromBgmOffset(double bgmOffset)
	{
		return bgmOffset == 0 ? 0.f : -bgmOffset * 1000;
	}

	bool SonolusEngine::fromBpmChangeEntity(const Sonolus::LevelDataEntity& bpmChangeEntity,
	                                        Tempo& tempo)
	{
		float beat;
		if (!bpmChangeEntity.tryGetDataValue("#BEAT", beat))
		{
			PRINT_DEBUG("Missing #BEAT key on #BPM_CHANGE");
			return false;
		}
		tempo.tick = beatsToTicks(beat);
		if (!bpmChangeEntity.tryGetDataValue("#BPM", tempo.bpm))
		{
			PRINT_DEBUG("Missing #BPM key on #BPM_CHANGE");
			return false;
		}
		return true;
	}

	SonolusEngine::TickType SonolusEngine::beatsToTicks(RealType beats, TickType beatTicks)
	{
		return std::lround(beats * beatTicks);
	}

	SonolusEngine::WidthType SonolusEngine::sizeToWidth(RealType size) { return size * 2; }

	SonolusEngine::LaneType SonolusEngine::toNativeLane(RealType lane, RealType size)
	{
		return lane - size + 6;
	}

	inline static bool stringMatching(const std::string& source, const std::string_view& toMatch,
	                                  size_t& offset)
	{
		if (toMatch.size() > source.size() - offset)
			return false;
		std::string_view source_view = std::string_view(source).substr(offset, toMatch.size());
		bool result = source_view == toMatch;
		if (result == true)
			offset += toMatch.size();
		return result;
	}

	inline static bool stringMatchAll(const std::string& source, const std::string_view& toMatch,
	                                  size_t offset)
	{
		return std::string_view(source).substr(offset, toMatch.size()) == toMatch;
	}

#pragma endregion

#pragma region PysekaiEngine
	LevelData PySekaiEngine::serialize(const Score& score)
	{
		IdManager idMgr(16);
		LevelData levelData;
		const auto getEnityName = [&](size_t idx)
		{
			auto& entity = levelData.entities[idx];
			if (entity.name.empty())
				entity.name = idMgr.getNextRef();
			return entity.name;
		};
		levelData.bgmOffset = toBgmOffset(score.metadata.musicOffset);
		levelData.entities.reserve(score.hiSpeedChanges.size() + score.layers.size() +
		                           score.notes.size() * 2 + score.tempoChanges.size());
		levelData.entities.emplace_back("Initialization");

		for (const auto& tempo : score.tempoChanges)
			levelData.entities.emplace_back(toBpmChangeEntity(tempo));

		std::vector<size_t> groupEntIdx;
		groupEntIdx.reserve(score.layers.size());
		for (const auto& layer : score.layers)
		{
			groupEntIdx.push_back(levelData.entities.size());
			levelData.entities.emplace_back(toGroupEntity(layer));
		}

		std::vector<size_t> lastSpeedIdx = groupEntIdx;
		std::multimap<int, const HiSpeedChange*> orderedSpeedChange;
		for (const auto& [_, speed] : score.hiSpeedChanges)
			orderedSpeedChange.emplace(speed.tick, &speed);

		for (const auto& [_, speedPtr] : orderedSpeedChange)
		{
			const HiSpeedChange& speed = *speedPtr;
			size_t newSpeedIdx = levelData.entities.size();
			auto& newSpeedEnt = levelData.entities.emplace_back(
			    toTimeScaleEntity(speed, getEnityName(groupEntIdx[speed.layer])));
			newSpeedEnt.name = idMgr.getNextRef();
			size_t& lastSpeedIndex = lastSpeedIdx[speed.layer];
			if (lastSpeedIndex == groupEntIdx[speed.layer])
				levelData.entities[lastSpeedIndex].data["first"] = newSpeedEnt.name;
			else
				levelData.entities[lastSpeedIndex].data["next"] = newSpeedEnt.name;
			lastSpeedIndex = newSpeedIdx;
		}

		std::multimap<TickType, size_t> simBuilder;
		for (const auto& [id, note] : score.notes)
		{
			if (note.getType() != NoteType::Tap && note.getType() != NoteType::Damage)
				continue;
			simBuilder.emplace(note.tick, levelData.entities.size());
			levelData.entities.emplace_back(toNoteEntity(note, getTapNoteArchetype(note),
			                                             getEnityName(groupEntIdx[note.layer])));
		}

		std::vector<size_t> entityJoints;
		std::vector<std::pair<size_t, size_t>> attachEntities;
		for (const auto& [id, hold] : score.holdNotes)
		{
			entityJoints.clear();
			attachEntities.clear();

			const Note &startNote = score.notes.at(hold.start.ID),
			           &endNote = score.notes.at(hold.end);
			const HoldStep endStep = { hold.end, HoldStepType::Normal, EaseType::Linear };
			size_t lastEntityIndex = levelData.entities.size();
			const RealType totalSteps = hold.steps.size() + 1;

			entityJoints.push_back(lastEntityIndex);
			if (hold.startType == HoldNoteType::Normal)
				simBuilder.emplace(startNote.tick, lastEntityIndex);
			levelData.entities.emplace_back(toNoteEntity(startNote,
			                                             getHoldNoteArchetype(startNote, hold),
			                                             getEnityName(groupEntIdx[startNote.layer]),
			                                             &hold, hold.start.type, hold.start.ease));

			for (size_t stepIdx = 0; stepIdx <= hold.steps.size(); ++stepIdx)
			{
				const HoldStep& step = stepIdx < hold.steps.size() ? hold.steps[stepIdx] : endStep;
				const Note& tickNote = score.notes.at(step.ID);
				RefType entName = idMgr.getRef(tickNote.ID);
				levelData.entities[lastEntityIndex].data.emplace("next", entName);
				lastEntityIndex = levelData.entities.size();
				levelData.entities
				    .emplace_back(toNoteEntity(tickNote, getHoldNoteArchetype(tickNote, hold),
				                               getEnityName(groupEntIdx[tickNote.layer]), &hold,
				                               step.type, step.ease))
				    .name = std::move(entName);
				if (step.type != HoldStepType::Skip)
					entityJoints.push_back(lastEntityIndex);
				else
					attachEntities.emplace_back(
					    std::make_pair(lastEntityIndex, entityJoints.size() - 1));
			}
			if (hold.endType == HoldNoteType::Normal)
				simBuilder.emplace(endNote.tick, lastEntityIndex);
			if (!hold.isGuide())
				levelData.entities[lastEntityIndex].data.emplace("activeHead",
				                                                 idMgr.getRef(startNote.ID));

			RefType segStartRef = idMgr.getRef(startNote.ID), segEndRef = idMgr.getRef(endNote.ID);
			RefType lastHeadRef = levelData.entities[entityJoints[0]].name = segStartRef;
			for (size_t connHeadIdx = 0, connTailIdx = 1; connTailIdx < entityJoints.size();
			     ++connHeadIdx, ++connTailIdx)
			{
				const auto& headEnt = levelData.entities[entityJoints[connHeadIdx]];
				const auto& tailEnt = levelData.entities[entityJoints[connTailIdx]];
				RefType tailRef = tailEnt.name;

				if (!hold.isGuide() && !hold.dummy)
					insertTransientTickNote(headEnt, tailEnt, connHeadIdx == 0, levelData.entities);

				levelData.entities.emplace_back(
				    toConnector(hold, lastHeadRef, tailRef, segStartRef, segEndRef));
				lastHeadRef = std::move(tailRef);
			}

			for (auto&& [entityIndex, jointIndex] : attachEntities)
			{
				auto& attachEntity = levelData.entities[entityIndex];
				auto& headEntity = levelData.entities[entityJoints[jointIndex]];
				auto& tailEntity = levelData.entities[entityJoints[jointIndex + 1]];
				attachEntity.data.emplace("attachHead", headEntity.name);
				attachEntity.data.emplace("attachTail", tailEntity.name);
				if (attachEntity.data.find("lane") != attachEntity.data.end())
					estimateAttachEntity(attachEntity, headEntity, tailEntity);
			}
		}

		std::vector<size_t> simEntities;
		for (auto it = simBuilder.begin(), end = simBuilder.end(); it != end;)
		{
			auto [startEnt, endEnt] = simBuilder.equal_range(it->first);
			simEntities.clear();
			std::transform(startEnt, endEnt, std::back_inserter(simEntities),
			               [](const std::multimap<TickType, size_t>::value_type& val)
			               { return val.second; });
			std::sort(simEntities.begin(), simEntities.end(),
			          [&](size_t aEnt, size_t bEnt)
			          {
				          return levelData.entities[aEnt].getDataValue<RealType>("lane") <
				                 levelData.entities[bEnt].getDataValue<RealType>("lane");
			          });

			for (size_t i = 1; i < simEntities.size(); ++i)
			{
				levelData.entities.push_back({ "SimLine",
				                               { { "left", getEnityName(simEntities[i - 1]) },
				                                 { "right", getEnityName(simEntities[i]) } } });
			}
			it = endEnt;
		}

		return levelData;
	}

	Score PySekaiEngine::deserialize(const LevelData& levelData)
	{
		size_t errorCount = 0, unsupportedCount = 0;
		Score score;
		score.metadata.musicOffset = fromBgmOffset(levelData.bgmOffset);
		const auto isBpmChangeEntity = [](const LevelDataEntity& e)
		{ return e.archetype == "#BPM_CHANGE"; };
		const auto isTimescaleGroupEntity = [](const LevelDataEntity& e)
		{ return e.archetype == "#TIMESCALE_GROUP"; };
		const auto isNoteEntity = [](const Sonolus::LevelDataEntity& e)
		{ return IO::endsWith(e.archetype, "Note") && e.archetype != "TransientHiddenTickNote"; };

		std::unordered_map<RefType, size_t> entityNameMap;
		entityNameMap.reserve(std::max<size_t>(levelData.entities.size() / 2, 32));
		std::vector<size_t> noteEntities;
		std::unordered_map<RefType, size_t> notePrevMap;
		const auto hasParentNote = [&](const LevelDataEntity& e)
		{ return e.name.size() && notePrevMap.find(e.name) != notePrevMap.end(); };
		const auto findPrevHold = [&](const LevelDataEntity& e) -> const LevelDataEntity*
		{
			const LevelDataEntity* p = &e;
			if (!hasParentNote(*p))
				return nullptr;
			int isSeparator = 0;
			do
				p = &levelData.entities[notePrevMap[p->name]];
			while (hasParentNote(*p) &&
			       !(p->tryGetDataValue("isSeparator", isSeparator) && isSeparator));
			return p;
		};
		for (size_t i = 0; i < levelData.entities.size(); ++i)
		{
			const auto& entity = levelData.entities[i];
			if (!entity.name.empty())
				entityNameMap.emplace(entity.name, i);
			if (isNoteEntity(entity))
			{
				noteEntities.push_back(i);
				std::string nextNoteEntity;
				if (entity.tryGetDataValue("next", nextNoteEntity))
					notePrevMap.emplace(nextNoteEntity, i);
			}
		}

		score.tempoChanges.clear();
		for (const auto& bpmChangeEntity : levelData.entities)
		{
			if (!isBpmChangeEntity(bpmChangeEntity))
				continue;
			Tempo tempo;
			if (fromBpmChangeEntity(bpmChangeEntity, tempo))
				score.tempoChanges.emplace_back(std::move(tempo));
			else
				errorCount++;
		}
		if (score.tempoChanges.empty())
			score.tempoChanges.push_back(Tempo{});
		std::sort(score.tempoChanges.begin(), score.tempoChanges.end(),
		          [](const Tempo& t1, const Tempo& t2) { return t1.tick < t2.tick; });

		score.layers.clear();
		score.hiSpeedChanges.clear();
		std::unordered_map<RefType, size_t> groupNameMap;
		for (const auto& groupEntity : levelData.entities)
		{
			if (!isTimescaleGroupEntity(groupEntity))
				continue;

			size_t layerIdx = score.layers.size();
			Layer layer{ layerIdx ? "#" + std::to_string(layerIdx) : "default" };
			if (!fromGroupEntity(groupEntity, layer))
			{
				errorCount++;
				continue;
			}
			RefType nextSpeedChange;
			groupEntity.tryGetDataValue("first", nextSpeedChange);
			if (groupEntity.name.size())
				groupNameMap.emplace(groupEntity.name, layerIdx);
			score.layers.emplace_back(layer);

			while (!nextSpeedChange.empty())
			{
				auto it = entityNameMap.find(nextSpeedChange);
				if (it == entityNameMap.end())
				{
					PRINT_DEBUG("#TIMESCALE_CHANGE entity not found!");
					errorCount++;
					break;
				}
				int status;
				HiSpeedChange hispeed;
				if (!(status = fromTimeScaleEntity(levelData.entities[it->second], groupEntity,
				                                   hispeed, groupNameMap)))
				{
					errorCount++;
					continue;
				}
				if (status == UNSUPPORTED_ENUM)
					unsupportedCount++;
				hispeed.ID = getNextHiSpeedID();
				hispeed.layer = layerIdx;
				score.hiSpeedChanges.emplace(hispeed.ID, hispeed);

				nextSpeedChange.clear();
				levelData.entities[it->second].tryGetDataValue("next", nextSpeedChange);
			}
		}
		if (score.layers.empty())
			score.layers.emplace_back(Layer{ "default" });

		for (const auto& entIdx : noteEntities)
		{
			auto& tapNoteEntity = levelData.entities[entIdx];
			if (tapNoteEntity.dataExists("next") || hasParentNote(tapNoteEntity))
				continue;
			Note note(NoteType::Tap);
			int status;
			if (!(status = fromTapNoteEntity(tapNoteEntity, note, groupNameMap)))
			{
				errorCount++;
				continue;
			}
			if (status == UNSUPPORTED_ENUM)
				++unsupportedCount;
			note.ID = Note::getNextID();
			score.notes.emplace(note.ID, note);
		}

		std::vector<Note> holdNotes;
		for (const auto& entIdx : noteEntities)
		{
			RefType next;
			auto& holdStartEntity = levelData.entities[entIdx];
			if (!holdStartEntity.tryGetDataValue("next", next))
				continue;
			if (!holdStartEntity.getDataValue<IntegerType>("isSeparator") &&
			    hasParentNote(holdStartEntity))
				continue;
			bool validHold = false;
			holdNotes.clear();
			HoldNote hold;
			if (!fromStartHoldEntity(levelData.entities[entIdx],
			                         holdNotes.emplace_back(NoteType::Hold), hold, groupNameMap,
			                         findPrevHold(levelData.entities[entIdx])))
				next.clear();
			while (next.size())
			{
				auto& noteEntity = levelData.entities[entityNameMap[next]];
				next.clear();
				bool isHoldEnd = noteEntity.getDataValue<IntegerType>("isSeparator") ||
				                 !noteEntity.tryGetDataValue("next", next);
				HoldStep holdStep{ -1, HoldStepType::Normal, EaseType::Linear };
				auto toNote = isHoldEnd ? fromEndHoldEntity : fromHoldMidEntity;
				Note& stepNote =
				    holdNotes.emplace_back(isHoldEnd ? NoteType::HoldEnd : NoteType::HoldMid);
				if (!toNote(noteEntity, stepNote, hold, holdStep, groupNameMap))
					break;
				if (isHoldEnd)
				{
					if (!hold.isGuide() && hold.endType == HoldNoteType::Hidden)
						// Anchor type don't have critical info so we have to do this
						// to ensure the hold is valid
						stepNote.critical = holdNotes.front().critical; 
					validHold = true;
				}
				else
					hold.steps.push_back(holdStep);
			}
			if (!validHold || !isValidHoldNotes(holdNotes, hold))
			{
				errorCount++;
				continue;
			}

			// Assign valid ids
			for (auto& note : holdNotes)
			{
				note.ID = Note::getNextID();
				note.parentID = holdNotes.front().ID;
			}
			auto stepIt = ++holdNotes.begin();
			for (auto& step : hold.steps)
			{
				stepIt->critical = holdNotes.front().critical;
				step.ID = (stepIt++)->ID;
			}
			holdNotes.front().parentID = -1;
			for (auto& note : holdNotes)
				score.notes.emplace(note.ID, note);

			hold.start.ID = holdNotes.front().ID;
			hold.end = holdNotes.back().ID;
			score.holdNotes.emplace(hold.start.ID, std::move(hold));
			sortHoldSteps(score, hold);
		}

		LaneType lanes = MAX_NOTE_WIDTH / 2.f;
		for (const auto& [_, note] : score.notes)
		{
			LaneType noteMaxLane = std::max(std::abs(note.lane - MAX_NOTE_WIDTH / 2.f),
			                                note.lane + note.width - MAX_NOTE_WIDTH / 2.f);
			if (noteMaxLane > lanes)
				lanes = noteMaxLane;
		}
		score.metadata.laneExtension = lanes - 6;

		std::string message;
		if (unsupportedCount)
		{
			PRINT_DEBUG("Total of %zd unsupported feature(s) found", unsupportedCount);
			message += "Score contains unsupported features!\n\n";
		}
		if (errorCount)
		{
			PRINT_DEBUG("Total of %zd error(s) found", errorCount);
			message += IO::formatString("%s\n%s", getString("error_load_score_file"),
			                            getString("score_partially_missing"));
		}
		if (errorCount || unsupportedCount)
			throw PartialScoreDeserializeError(score, message);
		return score;
	}

	bool PySekaiEngine::canSerialize(const Score& score) { return true; }

	LevelDataEntity PySekaiEngine::toGroupEntity(const Layer& layer)
	{
		return { "#TIMESCALE_GROUP",
			     { { "editorName", layer.name }, { "editorHidden", layer.hidden ? 1 : 0 } } };
	}

	LevelDataEntity PySekaiEngine::toTimeScaleEntity(const HiSpeedChange& hispeed,
	                                                 const RefType& groupName)
	{
		return { "#TIMESCALE_CHANGE",
			     { { "#BEAT", ticksToBeats(hispeed.tick) },
			       { "#TIMESCALE", roundOff(hispeed.speed) },
			       { "#TIMESCALE_SKIP", hispeed.skips },
			       { "#TIMESCALE_EASE", static_cast<int>(hispeed.ease) },
			       { "#TIMESCALE_GROUP", groupName },
			       { "hideNotes", static_cast<int>(hispeed.hideNotes) } } };
	}

	LevelDataEntity PySekaiEngine::toNoteEntity(const Note& note, const std::string& archetype,
	                                            const RefType& groupName, const HoldNote* hold,
	                                            HoldStepType step, EaseType easing)
	{
		float alpha = 1.f;
		if (hold)
		{
			switch (hold->fadeType)
			{
			default:
			case FadeType::Out:
				alpha = hold->end == note.ID ? 0.f : 1.f;
				break;
			case FadeType::In:
				alpha = hold->start.ID == note.ID ? 0.f : 1.f;
				break;
			case FadeType::None:
				break;
			}
		}

		return { archetype,
			     { { "#TIMESCALE_GROUP", groupName },
			       { "#BEAT", ticksToBeats(note.tick) },
			       { "lane", toSonolusLane(note.lane, note.width) },
			       { "size", widthToSize(note.width) },
			       { "direction", toDirectionNumeric(note.flick) },
			       { "isAttached", step == HoldStepType::Skip ? 1 : 0 },
			       { "isSeparator", 0 },
			       { "connectorEase", toEaseNumeric(easing) },
			       { "segmentKind", toKindNumeric(note.critical, hold) },
			       { "segmentAlpha", alpha },
				   { "segmentLayer", 0 },
			       { "effectKind", 0 } } };
	}

	LevelDataEntity PySekaiEngine::toConnector(const HoldNote& hold, const RefType& head,
	                                           const RefType& tail, const RefType& segmentHead,
	                                           const RefType& segmentTail)
	{
		LevelDataEntity::MapDataType data = {
			{ "head", head },
			{ "tail", tail },
		};
		if (hold.isGuide())
		{
			data.emplace("segmentHead", segmentHead);
			data.emplace("segmentTail", segmentTail);
		}
		else
		{
			data.emplace("activeHead", segmentHead);
			data.emplace("activeTail", segmentTail);
			data.emplace("segmentHead", segmentHead);
			data.emplace("segmentTail", segmentTail);
		}
		return { "Connector", std::move(data) };
	}

	std::string PySekaiEngine::getTapNoteArchetype(const Note& note)
	{
		std::string archetype = (note.dummy ? "Fake" : "");
		switch (note.getType())
		{
		case NoteType::Tap:
			archetype += (note.critical ? "Critical" : "Normal");
			if (note.friction)
				archetype += "Trace";
			if (note.isFlick())
				archetype += "Flick";
			if (!note.friction && !note.isFlick())
				archetype += "Tap";
			break;
		case NoteType::Damage:
			archetype += "Damage";
			break;
		default:
			PRINT_DEBUG("Unknown tap note");
			archetype += "Anchor";
			break;
		}
		archetype += "Note";
		return archetype;
	}

	std::string PySekaiEngine::getHoldNoteArchetype(const Note& note, const HoldNote& holdNote)
	{
		std::string archetype = (note.dummy ? "Fake" : "");
		// No need to handle Damage type yet, Hold can only have normal note
		archetype += (note.critical ? "Critical" : "Normal");
		if (note.ID == holdNote.start.ID)
		{
			if (holdNote.startType != HoldNoteType::Normal)
				return "AnchorNote";
			archetype += "Head";
			if (note.friction)
				archetype += "Trace";
			else
				archetype += "Tap";
		}
		else if (note.ID == holdNote.end)
		{
			if (holdNote.endType != HoldNoteType::Normal)
				return "AnchorNote";
			archetype += "Tail";
			if (note.friction)
				archetype += "Trace";
			if (note.isFlick())
				archetype += "Flick";
			if (!note.friction && !note.isFlick())
				archetype += "Release";
		}
		else
		{
			int idx = findHoldStep(holdNote, note.ID);
			switch (holdNote.steps[idx].type)
			{
			case HoldStepType::Normal:
			case HoldStepType::Skip:
				archetype += "Tick";
				break;
			case HoldStepType::Hidden:
				return "AnchorNote";
			default: // Do something about this
				archetype += "Tap";
				break;
			}
		}
		archetype += "Note";
		return archetype;
	}

	void PySekaiEngine::insertTransientTickNote(const Sonolus::LevelDataEntity& head,
	                                            const Sonolus::LevelDataEntity& tail, bool isHead,
	                                            std::vector<Sonolus::LevelDataEntity>& entities)
	{
		double headHalfBeat;
		double headFracHalfBeat =
		    std::modf(head.getDataValue<RealType>("#BEAT") * 2, &headHalfBeat);
		bool skips = (isHead || headFracHalfBeat != 0) ? 1 : 0;
		int endHalfBeat = std::ceil(tail.getDataValue<RealType>("#BEAT") * 2);
		// Copying the name since they are apart of entities list. They may relocate when inserting.
		std::string headName = head.name, tailName = tail.name;
		for (int halfBeat = headHalfBeat + skips; halfBeat < endHalfBeat; ++halfBeat)
		{
			entities.emplace_back(LevelDataEntity{ "TransientHiddenTickNote",
			                                       { { "#BEAT", halfBeat / 2. },
			                                         { "isAttached", 1 },
			                                         { "attachHead", headName },
			                                         { "attachTail", tailName } } });
		}
	}

	void PySekaiEngine::estimateAttachEntity(Sonolus::LevelDataEntity& attach,
	                                         const Sonolus::LevelDataEntity& head,
	                                         const Sonolus::LevelDataEntity& tail)
	{
		int easeNumeric;
		RealType headBeat, headLane, headSize, tailBeat, tailLane, tailSize, attachBeat;
		if (!head.tryGetDataValue("#BEAT", headBeat) || !head.tryGetDataValue("lane", headLane) ||
		    !head.tryGetDataValue("size", headSize) || !tail.tryGetDataValue("#BEAT", tailBeat) ||
		    !tail.tryGetDataValue("lane", tailLane) || !tail.tryGetDataValue("size", tailSize) ||
		    !head.tryGetDataValue("connectorEase", easeNumeric) ||
		    !attach.tryGetDataValue("#BEAT", attachBeat))
			return;
		RealType ratio = unlerpD(headBeat, tailBeat, attachBeat);
		float (*easeFunc)(float, float, float);
		switch (easeNumeric)
		{
		default:
		case 1:
			easeFunc = lerp;
			break;
		case 2:
			easeFunc = easeIn;
			break;
		case 3:
			easeFunc = easeOut;
			break;
		}
		attach.data["lane"] = easeFunc(headLane, tailLane, ratio);
		attach.data["size"] = easeFunc(headSize, tailSize, ratio);
	}

	int PySekaiEngine::toDirectionNumeric(FlickType flick)
	{
		switch (flick)
		{
		case FlickType::Left:
			return 1;
		case FlickType::Right:
			return 2;
		case FlickType::Down:
			return 3;
		case FlickType::DownLeft:
			return 4;
		case FlickType::DownRight:
			return 5;
		default:
			PRINT_DEBUG("Unknown FlickType");
			[[fallthrough]];
		case FlickType::None:
		case FlickType::Default:
			return 0;
		}
	}

	int PySekaiEngine::toEaseNumeric(EaseType ease)
	{
		switch (ease)
		{
		case EaseType::Linear:
			return 1;
		case EaseType::EaseIn:
			return 2;
		case EaseType::EaseOut:
			return 3;
		case EaseType::EaseInOut:
			return 4;
		case EaseType::EaseOutIn:
			return 5;
		default:
			PRINT_DEBUG("Unknown EaseType");
			return 1;
		}
	}

	int PySekaiEngine::toKindNumeric(bool critical, const HoldNote* hold)
	{
		bool isGuideHold = hold && hold->isGuide();
		if (!isGuideHold)
		{
			bool isDummyHold = hold && hold->dummy;
			if (!isDummyHold)
				return (critical ? 2 : 1);
			else
				return (critical ? 52 : 51);
		}
		else
		{
			switch (hold->guideColor)
			{
			case GuideColor::Neutral:
				return 101;
			case GuideColor::Red:
				return 102;
			case GuideColor::Green:
				return 103;
			case GuideColor::Blue:
				return 104;
			case GuideColor::Yellow:
				return 105;
			case GuideColor::Purple:
				return 106;
			case GuideColor::Cyan:
				return 107;
			case GuideColor::Black:
				return 108;
			default:
				PRINT_DEBUG("Unknown segment kind");
				return 0;
			}
		}
	}

	bool PySekaiEngine::fromGroupEntity(const LevelDataEntity& groupEntity, Layer& layer)
	{
		groupEntity.tryGetDataValue("editorName", layer.name);
		groupEntity.tryGetDataValue("editorHidden", layer.hidden);
		return true;
	}

	int PySekaiEngine::fromTimeScaleEntity(const LevelDataEntity& timescaleEntity,
	                                       const LevelDataEntity& groupEntity,
	                                       HiSpeedChange& hispeed,
	                                       std::unordered_map<RefType, size_t>& groupNameMap)
	{
		if (timescaleEntity.archetype != "#TIMESCALE_CHANGE")
		{
			PRINT_DEBUG("Mismatch entity archetype. Expected #TIMESCALE_CHANGE got '%s' instead",
			            timescaleEntity.archetype.c_str());
			return false;
		}
		std::string groupName;
		float beat;
		int easing = 0;
		if (!timescaleEntity.tryGetDataValue("#BEAT", beat))
		{
			PRINT_DEBUG("Missing #BEAT key on #TIMESCALE_CHANGE");
			return false;
		}
		hispeed.tick = beatsToTicks(beat);
		if (!timescaleEntity.tryGetDataValue("#TIMESCALE", hispeed.speed))
		{
			PRINT_DEBUG("Missing #TIMESCALE key on #TIMESCALE_CHANGE");
			return false;
		}
		if (!timescaleEntity.tryGetDataValue("#TIMESCALE_GROUP", groupName))
		{
			PRINT_DEBUG("Missing #TIMESCALE_GROUP key on #TIMESCALE_CHANGE");
			return false;
		}
		if (groupName != groupEntity.name)
		{
			PRINT_DEBUG("#TIMESCALE_CHANGE of group '%s' referencing group '%s'",
			            groupEntity.name.c_str(), groupName.c_str());
			return false;
		}
		timescaleEntity.tryGetDataValue("#TIMESCALE_SKIP", hispeed.skips);
		if (timescaleEntity.tryGetDataValue("#TIMESCALE_EASE", easing))
		{
			if (easing < 0 || easing >= static_cast<int>(HiSpeedEaseType::EaseTypeCount))
			{
				PRINT_DEBUG("Unknown ease type for hispeed!");
				return false;
			}
			hispeed.ease = static_cast<HiSpeedEaseType>(easing);
		}
		timescaleEntity.tryGetDataValue("hideNotes", hispeed.hideNotes);
		return true;
	}

	int PySekaiEngine::fromNoteEntity(const LevelDataEntity& noteEntity, Note& note,
	                                  const std::unordered_map<RefType, size_t>& groupNameMap)
	{
		RealType beat, lane, size;
		std::string group;
		if (!noteEntity.tryGetDataValue("#BEAT", beat))
		{
			PRINT_DEBUG("Missing #BEAT key on %s (%s)", noteEntity.archetype.c_str(),
			            noteEntity.name.c_str());
			return false;
		}
		if (!noteEntity.tryGetDataValue("lane", lane))
		{
			PRINT_DEBUG("Missing 'lane' key on %s (%s)", noteEntity.archetype.c_str(),
			            noteEntity.name.c_str());
			return false;
		}
		if (!noteEntity.tryGetDataValue("size", size))
		{
			PRINT_DEBUG("Missing 'size' key on %s (%s)", noteEntity.archetype.c_str(),
			            noteEntity.name.c_str());
			return false;
		}
		note.tick = beatsToTicks(beat);
		note.width = sizeToWidth(size);
		note.lane = toNativeLane(lane, size);
		note.layer = 0;
		if (noteEntity.tryGetDataValue("#TIMESCALE_GROUP", group))
		{
			auto it = groupNameMap.find(group);
			if (it == groupNameMap.end())
			{
				PRINT_DEBUG("%s (%s) referencing non group '%s'", noteEntity.archetype.c_str(),
				            noteEntity.name.c_str(), group.c_str());
				return false;
			}
			note.layer = it->second;
		}
		int effect = 0;
		noteEntity.tryGetDataValue("effectKind", effect);
		if (!isValidNoteState(note))
		{
			PRINT_DEBUG("Invalid note state (t:%d, w:%f, l:%f) on %s (%s)", note.tick, note.width,
			            note.lane, noteEntity.archetype.c_str(), noteEntity.name.c_str());
			return false;
		}
		return effect != 0 ? UNSUPPORTED_ENUM : true;
	}

	int PySekaiEngine::fromTapNoteEntity(const LevelDataEntity& tapNoteEntity, Note& note,
	                                     const std::unordered_map<RefType, size_t>& groupNameMap)
	{
		int status;
		if (!(status = fromNoteEntity(tapNoteEntity, note, groupNameMap)))
			return false;

		size_t offset = 0;
		if (stringMatching(tapNoteEntity.archetype, "Fake", offset))
		{
			note.dummy = true;
		}
		if (stringMatchAll(tapNoteEntity.archetype, "DamageNote", offset))
		{
			Note damage(NoteType::Damage);
			damage.dummy = note.dummy;
			damage.tick = note.tick;
			damage.lane = note.lane;
			damage.width = note.width;
			note = damage;
			return true;
		}
		if (stringMatching(tapNoteEntity.archetype, "Critical", offset))
			note.critical = true;
		else if (!stringMatching(tapNoteEntity.archetype, "Normal", offset))
		{
			PRINT_DEBUG("Invalid note archetype %s (%s)", tapNoteEntity.archetype.c_str(),
			            tapNoteEntity.name.c_str());
			return false;
		}
		bool hasModifier = false;
		if (stringMatching(tapNoteEntity.archetype, "Trace", offset))
		{
			hasModifier = true;
			note.friction = true;
		}
		if (stringMatching(tapNoteEntity.archetype, "Flick", offset))
		{
			hasModifier = true;
			int direction;
			if (!tapNoteEntity.tryGetDataValue("direction", direction))
			{
				PRINT_DEBUG("Missing 'direction' key on %s (%s)", tapNoteEntity.archetype.c_str(),
				            tapNoteEntity.name.c_str());
				return false;
			}
			note.flick = fromDirectionNumeric(direction);
			if (note.flick == FlickType::FlickTypeCount)
			{
				PRINT_DEBUG("Unknown direction value %d!", direction);
				return false;
			}
		}
		if ((!hasModifier && !stringMatching(tapNoteEntity.archetype, "Tap", offset)) ||
		    !stringMatchAll(tapNoteEntity.archetype, "Note", offset))
		{
			PRINT_DEBUG("Invalid note archetype %s (%s)", tapNoteEntity.archetype.c_str(),
			            tapNoteEntity.name.c_str());
			return false;
		}
		return status;
	}

	int PySekaiEngine::fromStartHoldEntity(const LevelDataEntity& noteEntity, Note& startNote,
	                                       HoldNote& hold,
	                                       const std::unordered_map<RefType, size_t>& groupNameMap,
	                                       const Sonolus::LevelDataEntity* prevNoteEntity)
	{
		int status;
		if (!(status = fromNoteEntity(noteEntity, startNote, groupNameMap)))
			return false;

		int kind = 0;
		if (noteEntity.tryGetDataValue("segmentKind", kind))
		{
			if (!fromKindNumeric(kind, hold, startNote))
			{
				PRINT_DEBUG("Unsupported segmentKind %d", kind);
				return false;
			}
		}
		else
		{
			PRINT_DEBUG("Hold note doesn't have a valid segmentKind!");
			return false;
		}

		// Handle separate holds
		// hold -> guide: set anchor
		// hold -> hold: set anchor
		// guide -> hold: no force anchor
		// guide -> guide: force anchor
		bool forceAnchor = false;
		int isSeperator = 0;
		noteEntity.tryGetDataValue("isSeparator", isSeperator);
		if (prevNoteEntity && isSeperator)
		{
			int prevKind;
			if (!prevNoteEntity->tryGetDataValue("segmentKind", prevKind))
				return false;
			forceAnchor = !isGuideKind(prevKind);
		}

		float alpha;
		if (hold.isGuide() && noteEntity.tryGetDataValue("segmentAlpha", alpha))
		{
			if (isClose(alpha, 1.f))
				hold.fadeType = FadeType::Out;
			else if (isClose(alpha, 0.f))
				hold.fadeType = FadeType::In;
			else
			{
				PRINT_DEBUG("Custom fading %.4f!", alpha);
				return false;
			}
		}

		int ease;
		if (noteEntity.tryGetDataValue("connectorEase", ease))
		{
			hold.start.ease = fromEaseNumeric(ease);
			if (hold.start.ease == EaseType::EaseTypeCount)
			{
				PRINT_DEBUG("Unknown connectorEase %d", ease);
				return false;
			}
		}

		if (forceAnchor)
		{
			if (!hold.isGuide())
				hold.startType = HoldNoteType::Hidden;
			hold.start.type = HoldStepType::Hidden;
			return true;
		}

		size_t offset = 0;
		if (stringMatching(noteEntity.archetype, "Fake", offset))
			startNote.dummy = true;
		if (stringMatchAll(noteEntity.archetype, "AnchorNote", offset))
		{
			if (!hold.isGuide())
				hold.startType = HoldNoteType::Hidden;
			hold.start.type = HoldStepType::Hidden;
			return true;
		}
		else if (hold.isGuide())
		{
			PRINT_DEBUG("None anchor note inside of guide");
			return false;
		}
		if (stringMatchAll(noteEntity.archetype, "DamageNote", offset))
		{
			PRINT_DEBUG("Damage note as hold start!");
			return false;
		}
		if (stringMatching(noteEntity.archetype, "Critical", offset))
		{
			if (!startNote.critical)
			{
				PRINT_DEBUG("Crit hold start with a normal tap!");
				return false;
			}
		}
		else if (stringMatching(noteEntity.archetype, "Normal", offset))
		{
			if (startNote.critical)
			{
				PRINT_DEBUG("Normal hold start with a crit tap!");
				return false;
			}
		}
		else
		{
			PRINT_DEBUG("Invalid note archetype %s (%s)", noteEntity.archetype.c_str(),
			            noteEntity.name.c_str());
			return false;
		}
		if (!stringMatching(noteEntity.archetype, "Head", offset))
		{
			PRINT_DEBUG("Mising Head in archetype %s", noteEntity.archetype.c_str());
			return false;
		}
		bool hasModifier = false;
		if (stringMatching(noteEntity.archetype, "Trace", offset))
		{
			hasModifier = true;
			startNote.friction = true;
		}
		if (stringMatching(noteEntity.archetype, "Flick", offset))
		{
			PRINT_DEBUG("Unsupported flick on head note");
			return false;
		}
		if ((!hasModifier && !stringMatching(noteEntity.archetype, "Tap", offset)) ||
		    !stringMatchAll(noteEntity.archetype, "Note", offset))
		{
			PRINT_DEBUG("Invalid note archetype %s (%s)", noteEntity.archetype.c_str(),
			            noteEntity.name.c_str());
			return false;
		}
		return status;
	}

	int PySekaiEngine::fromHoldMidEntity(const Sonolus::LevelDataEntity& noteEntity, Note& note,
	                                     HoldNote& hold, HoldStep& step,
	                                     const std::unordered_map<RefType, size_t>& groupNameMap)
	{
		int status;
		if (!(status = fromNoteEntity(noteEntity, note, groupNameMap)))
			return false;

		int attached = 0;
		if (noteEntity.tryGetDataValue("isAttached", attached) && attached)
			step.type = HoldStepType::Skip;

		int ease;
		if (noteEntity.tryGetDataValue("connectorEase", ease))
		{
			step.ease = fromEaseNumeric(ease);
			if (step.ease == EaseType::EaseTypeCount)
			{
				PRINT_DEBUG("Unknown connectorEase %d", ease);
				return false;
			}
		}

		size_t offset = 0;
		if (stringMatching(noteEntity.archetype, "Fake", offset))
			note.dummy = true;
		if (stringMatchAll(noteEntity.archetype, "AnchorNote", offset))
		{
			step.type = HoldStepType::Hidden;
			return true;
		}
		else if (hold.isGuide())
		{
			PRINT_DEBUG("None anchor note inside of guide");
			return false;
		}
		if (stringMatchAll(noteEntity.archetype, "DamageNote", offset))
		{
			PRINT_DEBUG("Damage note in hold!");
			return false;
		}
		if (stringMatching(noteEntity.archetype, "Critical", offset))
			note.critical = true;
		else if (!stringMatching(noteEntity.archetype, "Normal", offset))
		{
			PRINT_DEBUG("Invalid note archetype %s (%s)", noteEntity.archetype.c_str(),
			            noteEntity.name.c_str());
			return false;
		}
		if (!stringMatchAll(noteEntity.archetype, "TickNote", offset))
		{
			step.type = HoldStepType::Normal;
			PRINT_DEBUG("Non tick note inside of hold");
			return false;
		}
		return status;
	}

	int PySekaiEngine::fromEndHoldEntity(const Sonolus::LevelDataEntity& noteEntity, Note& note,
	                                     HoldNote& hold, HoldStep& step,
	                                     const std::unordered_map<RefType, size_t>& groupNameMap)
	{
		int status;
		if (!(status = fromNoteEntity(noteEntity, note, groupNameMap)))
			return false;

		// Handle separate holds
		// hold -> guide: force release
		// hold -> hold:
		// guide -> hold:
		// guide -> guide: force anchor
		bool forceGuideAnchor = true, forceTail = true;
		if (noteEntity.getDataValue<int>("isSeparator"))
		{
			int kind = 0;
			if (noteEntity.tryGetDataValue("segmentKind", kind))
			{
				forceGuideAnchor = hold.isGuide() && isGuideKind(kind);
				forceTail = !hold.isGuide() && isGuideKind(kind);
			}
		}

		float alpha;
		if (hold.isGuide() && noteEntity.tryGetDataValue("segmentAlpha", alpha))
		{
			if (!isClose(alpha, 1.f) && (!isClose(alpha, 0.f) || hold.fadeType != FadeType::Out))
			{

				PRINT_DEBUG("Custom fading %.4f!", alpha);
				return false;
			}
			if (isClose(alpha, 1.f) && hold.fadeType == FadeType::Out)
				hold.fadeType = FadeType::None;
		}

		size_t offset = 0;
		if (stringMatching(noteEntity.archetype, "Fake", offset))
			note.dummy = true;
		if (stringMatchAll(noteEntity.archetype, "AnchorNote", offset))
		{
			if (!hold.isGuide())
				hold.endType = HoldNoteType::Hidden;
			return true;
		}
		else if (hold.isGuide())
		{
			if (!forceGuideAnchor)
				return true;
			PRINT_DEBUG("None anchor note inside of guide");
			return false;
		}
		if (stringMatchAll(noteEntity.archetype, "DamageNote", offset))
		{
			PRINT_DEBUG("Damage note as hold end!");
			return false;
		}
		if (stringMatching(noteEntity.archetype, "Critical", offset))
			note.critical = true;
		else if (!stringMatching(noteEntity.archetype, "Normal", offset))
		{
			PRINT_DEBUG("Invalid note archetype %s (%s)", noteEntity.archetype.c_str(),
			            noteEntity.name.c_str());
			return false;
		}
		if (forceTail && !stringMatching(noteEntity.archetype, "Tail", offset))
		{
			PRINT_DEBUG("Mising Tail in archetype %s", noteEntity.archetype.c_str());
			return false;
		}
		bool hasModifier = false;
		if (stringMatching(noteEntity.archetype, "Trace", offset))
		{
			hasModifier = true;
			note.friction = true;
		}
		if (stringMatching(noteEntity.archetype, "Flick", offset))
		{
			hasModifier = true;
			int direction;
			if (!noteEntity.tryGetDataValue("direction", direction))
			{
				PRINT_DEBUG("Missing 'direction' key on %s (%s)", noteEntity.archetype.c_str(),
				            noteEntity.name.c_str());
				return false;
			}
			note.flick = fromDirectionNumeric(direction);
			if (note.flick == FlickType::FlickTypeCount)
			{
				PRINT_DEBUG("Unknown direction value %d!", direction);
				return false;
			}
		}
		if ((!hasModifier && !stringMatching(noteEntity.archetype, "Release", offset)) ||
		    !stringMatchAll(noteEntity.archetype, "Note", offset))
		{
			PRINT_DEBUG("Invalid note archetype %s (%s)", noteEntity.archetype.c_str(),
			            noteEntity.name.c_str());
			return false;
		}
		return status;
	}

	bool PySekaiEngine::isValidNoteState(const Note& note)
	{
		return note.tick >= 0 && note.width >= 0;
	}

	bool PySekaiEngine::isValidHoldNotes(const std::vector<Note>& holdNotes, const HoldNote& hold)
	{
		if (holdNotes.size() < 2)
		{
			PRINT_DEBUG("Hold notes missing start/end");
			return false;
		}
		const Note& startNote = holdNotes.front();
		const Note& endNote = holdNotes.back();
		if (startNote.isFlick() || (startNote.critical && !endNote.critical) ||
		    (!startNote.critical && endNote.critical && !endNote.friction && !endNote.isFlick()))
		{
			PRINT_DEBUG("Invalid start-end hold note configuration");
			return false;
		}
		for (size_t i = 0; i < hold.steps.size(); i++)
		{
			if (holdNotes[i + 1].tick < startNote.tick || holdNotes[i + 1].tick > endNote.tick)
			{
				PRINT_DEBUG("Tick note outside hold note range!");
				return false;
			}
			if (hold.steps[i].type != HoldStepType::Hidden &&
			    holdNotes[i + 1].critical != startNote.critical)
			{
				PRINT_DEBUG("Tick note critical mismatch with the rest of the slide!");
				return false;
			}
		}
		return true;
	}

	FlickType PySekaiEngine::fromDirectionNumeric(int direction)
	{
		switch (direction)
		{
		case 0:
			return FlickType::Default;
		case 1:
			return FlickType::Left;
		case 2:
			return FlickType::Right;
		case 3:
			return FlickType::Down;
		case 4:
			return FlickType::DownLeft;
		case 5:
			return FlickType::DownRight;
		default:
			return FlickType::FlickTypeCount;
		}
	}

	EaseType PySekaiEngine::fromEaseNumeric(int ease)
	{
		switch (ease)
		{
		case 1:
			return EaseType::Linear;
		case 2:
			return EaseType::EaseIn;
		case 3:
			return EaseType::EaseOut;
		case 4:
			return EaseType::EaseInOut;
		case 5:
			return EaseType::EaseOutIn;
		default:
			return EaseType::EaseTypeCount;
		}
	}

	bool PySekaiEngine::fromKindNumeric(int kind, HoldNote& hold, Note& note)
	{
		if (isGuideKind(kind))
		{
			hold.startType = hold.endType = HoldNoteType::Guide;
			hold.guideColor = static_cast<GuideColor>(kind - 101);
			return true;
		}
		if (50 < kind)
		{
			hold.dummy = true;
			kind -= 50;
		}
		switch (kind)
		{
		case 1:
			return true;
		case 2:
			note.critical = true;
			return true;
		default:
			return false;
		}
	}

	bool PySekaiEngine::isGuideKind(int kind) { return 101 <= kind && kind <= 108; }
#pragma endregion

}