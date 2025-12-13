#include "NativeScoreSerializer.h"

namespace MikuMikuWorld
{
	// Version 1: Version + Metadata(title, author, artist, musicFile, musicOffset) + TimeSignature
	// + TempoChange +	Note + Hold
	// Version 2: Skill Support, Jacket Support
	// Version 3: Hispeed Support, Store address offset (metadata, events, taps, holds)
	// Version 4: Guide Support, use platform path for metadata
	const int MMWS_VERSION = 4;
	const char* MMWS_SIGNATURE = "MMWS";
	// Version 1: Damage note support, Lane extension support
	// Version 2: Support hold fade type, Change version to int16
	// Version 3: Support more guide colors
	// Version 4: Support layers
	// Version 5: Support waypoints
	// Version 6: Use floating type for lane and width
	const int CC_MMWS_VERSION = 6;
	const char* CC_MMWS_SIGNATURE = "CCMMWS";
	// Version 1: Revert version to int32, support dummy note, dummy hold
	// Version 2: Support hispeed easing, skip
	const int UC_MMWS_VERSION = 2;
	const char* UC_MMWS_SIGNATURE = "UCMMWS";

	enum NoteFlags
	{
		NOTE_CRITICAL = 1 << 0,
		NOTE_FRICTION = 1 << 1,
		NOTE_DUMMY = 1 << 2,
	};

	enum HoldFlags
	{
		HOLD_START_HIDDEN = 1 << 0,
		HOLD_END_HIDDEN = 1 << 1,
		HOLD_GUIDE = 1 << 2,
		HOLD_DUMMY = 1 << 3
	};

	struct NativeScoreSerializer::ScoreVersion
	{
		int version;
		int cyanvasVersion;
		int untitledVersion;

		ScoreVersion() : version(0), cyanvasVersion(0), untitledVersion(0) {}
		ScoreVersion(int ucVersion, int ccVersion = CC_MMWS_VERSION, int version = MMWS_VERSION)
		    : version(version), cyanvasVersion(ccVersion), untitledVersion(ucVersion)
		{
		}

		inline bool supportSkillFever() const { return version >= 2; }
		inline bool supportJacket() const { return version >= 2; }
		inline bool supportAddress() const { return version >= 3; }
		inline bool supportHispeed() const { return version >= 3; }
		inline bool supportGuideNote() const { return version >= 4; }
		inline bool supportDamageNote() const { return cyanvasVersion >= 1; }
		inline bool supportLaneExtension() const { return cyanvasVersion >= 1; }
		inline bool supportFadeType() const { return cyanvasVersion >= 2; }
		inline bool supportGuideColor() const { return cyanvasVersion >= 3; }
		inline bool supportLayers() const { return cyanvasVersion >= 4; }
		inline bool supportWaypoints() const { return cyanvasVersion >= 5; }
		inline bool supportFloatingLaneWidth() const { return cyanvasVersion >= 6; }
		inline bool supportDummyNote() const { return cyanvasVersion >= 6 && untitledVersion >= 1; }
		inline bool supportHispeedSkipEase() const { return untitledVersion >= 2; }

		inline bool isSupportedVersion() const
		{
			return version <= MMWS_VERSION || cyanvasVersion <= CC_MMWS_VERSION ||
			       untitledVersion <= UC_MMWS_VERSION;
		}
	};

	Note NativeScoreSerializer::readNote(NoteType type, IO::BinaryReader& reader,
	                                     const ScoreVersion& version)
	{
		Note note(type);

		if (version.supportFloatingLaneWidth())
		{
			note.tick = reader.readUInt32();
			note.lane = reader.readSingle();
			note.width = reader.readSingle();
		}
		else
		{
			note.tick = reader.readUInt32();
			note.lane = reader.readUInt32();
			note.width = reader.readUInt32();
		}

		if (version.supportLayers())
			note.layer = reader.readUInt32();

		if (!note.hasEase())
			note.flick = static_cast<FlickType>(reader.readUInt32());

		unsigned int flags = reader.readUInt32();
		note.critical = (bool)(flags & NOTE_CRITICAL);
		note.friction = (bool)(flags & NOTE_FRICTION);
		note.dummy = version.supportDummyNote() && (bool)(flags & NOTE_DUMMY);

		return note;
	}

	void NativeScoreSerializer::writeNote(const Note& note, IO::BinaryWriter& writer)
	{
		writer.writeInt32(note.tick);
		writer.writeSingle(note.lane);
		writer.writeSingle(note.width);

		writer.writeInt32(note.layer);

		if (!note.hasEase())
			writer.writeInt32(static_cast<int>(note.flick));

		unsigned int flags{};
		if (note.critical)
			flags |= NOTE_CRITICAL;
		if (note.friction)
			flags |= NOTE_FRICTION;
		if (note.dummy)
			flags |= NOTE_DUMMY;
		writer.writeInt32(flags);
	}

	ScoreMetadata NativeScoreSerializer::readMetadata(IO::BinaryReader& reader,
	                                                  const ScoreVersion& version)
	{
		ScoreMetadata metadata;
		metadata.title = reader.readString();
		metadata.author = reader.readString();
		metadata.artist = reader.readString();
		metadata.musicFile = reader.readString();
		metadata.musicOffset = reader.readSingle();

		if (version.supportJacket())
			metadata.jacketFile = reader.readString();

		if (version.supportLaneExtension())
			metadata.laneExtension = reader.readUInt32();

		return metadata;
	}

	void NativeScoreSerializer::writeMetadata(const ScoreMetadata& metadata,
	                                          IO::BinaryWriter& writer)
	{
		writer.writeString(metadata.title);
		writer.writeString(metadata.author);
		writer.writeString(metadata.artist);
		writer.writeString(metadata.musicFile);
		writer.writeSingle(metadata.musicOffset);
		writer.writeString(metadata.jacketFile);
		writer.writeInt32(metadata.laneExtension);
	}

	void NativeScoreSerializer::readScoreEvents(Score& score, IO::BinaryReader& reader,
	                                            const ScoreVersion& version)
	{
		// time signature
		int timeSignatureCount = reader.readUInt32();
		if (timeSignatureCount)
			score.timeSignatures.clear();

		for (int i = 0; i < timeSignatureCount; ++i)
		{
			int measure = reader.readUInt32();
			int numerator = reader.readUInt32();
			int denominator = reader.readUInt32();
			score.timeSignatures[measure] = { measure, numerator, denominator };
		}

		// bpm
		int tempoCount = reader.readUInt32();
		if (tempoCount)
			score.tempoChanges.clear();

		for (int i = 0; i < tempoCount; ++i)
		{
			int tick = reader.readUInt32();
			float bpm = reader.readSingle();
			score.tempoChanges.push_back({ tick, bpm });
		}

		// hi-speed
		if (version.supportHispeed())
		{
			int hiSpeedCount = reader.readUInt32();
			for (int i = 0; i < hiSpeedCount; ++i)
			{
				int tick = reader.readUInt32();
				float speed = reader.readSingle();
				int layer = version.supportLayers() ? reader.readUInt32() : 0;
				float skip = version.supportHispeedSkipEase() ? reader.readSingle() : 0;
				HiSpeedEaseType ease = static_cast<HiSpeedEaseType>(version.supportHispeedSkipEase() ? reader.readInt32() : 0);
				id_t id = getNextHiSpeedID();
				score.hiSpeedChanges[id] = HiSpeedChange{ id, tick, speed, layer, skip, ease };
			}
		}

		// skills and fever
		if (version.supportSkillFever())
		{
			int skillCount = reader.readUInt32();
			for (int i = 0; i < skillCount; ++i)
			{
				int tick = reader.readUInt32();
				id_t id = getNextSkillID();
				score.skills.emplace(id, SkillTrigger{ id, tick });
			}

			score.fever.startTick = reader.readUInt32();
			score.fever.endTick = reader.readUInt32();
		}
	}

	void NativeScoreSerializer::writeScoreEvents(const Score& score, IO::BinaryWriter& writer)
	{
		writer.writeInt32(score.timeSignatures.size());
		for (const auto& [_, timeSignature] : score.timeSignatures)
		{
			writer.writeInt32(timeSignature.measure);
			writer.writeInt32(timeSignature.numerator);
			writer.writeInt32(timeSignature.denominator);
		}

		writer.writeInt32(score.tempoChanges.size());
		for (const auto& tempo : score.tempoChanges)
		{
			writer.writeInt32(tempo.tick);
			writer.writeSingle(tempo.bpm);
		}

		writer.writeInt32(score.hiSpeedChanges.size());
		for (const auto& [_, hiSpeed] : score.hiSpeedChanges)
		{
			writer.writeInt32(hiSpeed.tick);
			writer.writeSingle(hiSpeed.speed);
			writer.writeInt32(hiSpeed.layer);
			writer.writeSingle(hiSpeed.skips);
			writer.writeInt32(static_cast<int>(hiSpeed.ease));
		}

		writer.writeInt32(score.skills.size());
		for (const auto& [_, skill] : score.skills)
		{
			writer.writeInt32(skill.tick);
		}

		writer.writeInt32(score.fever.startTick);
		writer.writeInt32(score.fever.endTick);
	}

	Score NativeScoreSerializer::deserialize(std::string filename)
	{
		Score score;
		IO::BinaryReader reader(filename);
		if (!reader.isStreamValid())
			return score;

		std::string signature = reader.readString();
		if (signature != MMWS_SIGNATURE && signature != CC_MMWS_SIGNATURE &&
		    signature != UC_MMWS_SIGNATURE)
			throw std::runtime_error("Invalid MMWS file. Unrecognized signature");

		ScoreVersion version = signature == UC_MMWS_SIGNATURE ? ScoreVersion(reader.readUInt32())
		                                                      : signature == CC_MMWS_SIGNATURE
		                           ? ScoreVersion(0, std::max((int)reader.readUInt16(), 1), (int)reader.readUInt16())
		                           : ScoreVersion(0, 0, reader.readUInt32());

		if (!version.isSupportedVersion())
			throw std::runtime_error("Cannot open this file. The file was created in a newer version");

		uint32_t metadataAddress{};
		uint32_t eventsAddress{};
		uint32_t tapsAddress{};
		uint32_t holdsAddress{};
		uint32_t damagesAddress{};
		uint32_t layersAddress{};
		uint32_t waypointsAddress{};
		if (version.supportAddress())
		{
			metadataAddress = reader.readUInt32();
			eventsAddress = reader.readUInt32();
			tapsAddress = reader.readUInt32();
			holdsAddress = reader.readUInt32();
			if (version.supportDamageNote())
				damagesAddress = reader.readUInt32();
			if (version.supportLayers())
				layersAddress = reader.readUInt32();
			if (version.supportWaypoints())
				waypointsAddress = reader.readUInt32();

			reader.seek(metadataAddress);
		}

		score.metadata = readMetadata(reader, version);

		if (version.supportAddress())
			reader.seek(eventsAddress);

		readScoreEvents(score, reader, version);

		if (version.supportAddress())
			reader.seek(tapsAddress);

		int noteCount = reader.readUInt32();
		score.notes.reserve(noteCount);
		for (int i = 0; i < noteCount; ++i)
		{
			Note note = readNote(NoteType::Tap, reader, version);
			note.ID = Note::getNextID();
			score.notes[note.ID] = note;
		}

		if (version.supportAddress())
			reader.seek(holdsAddress);

		int holdCount = reader.readUInt32();
		score.holdNotes.reserve(holdCount);
		for (int i = 0; i < holdCount; ++i)
		{
			HoldNote hold;

			unsigned int flags{};
			if (version.supportGuideNote())
				flags = reader.readUInt32();

			if (flags & HOLD_START_HIDDEN)
				hold.startType = HoldNoteType::Hidden;

			if (flags & HOLD_END_HIDDEN)
				hold.endType = HoldNoteType::Hidden;

			if (flags & HOLD_GUIDE)
				hold.startType = hold.endType = HoldNoteType::Guide;

			if (flags & HOLD_DUMMY)
				hold.dummy = true;

			Note start = readNote(NoteType::Hold, reader, version);
			start.ID = Note::getNextID();
			hold.start.ease = static_cast<EaseType>(reader.readUInt32());
			hold.start.ID = start.ID;
			if (version.supportFadeType())
			{
				hold.fadeType = static_cast<FadeType>(reader.readUInt32());
			}
			if (version.supportGuideColor())
			{
				hold.guideColor = static_cast<GuideColor>(reader.readUInt32());
			}
			else
			{
				hold.guideColor = start.critical ? GuideColor::Yellow : GuideColor::Green;
			}
			score.notes[start.ID] = start;

			int stepCount = reader.readUInt32();
			hold.steps.reserve(stepCount);
			for (int i = 0; i < stepCount; ++i)
			{
				Note mid = readNote(NoteType::HoldMid, reader, version);
				mid.ID = Note::getNextID();
				mid.parentID = start.ID;
				score.notes[mid.ID] = mid;

				HoldStep step{};
				step.type = static_cast<HoldStepType>(reader.readUInt32());
				step.ease = static_cast<EaseType>(reader.readUInt32());
				step.ID = mid.ID;
				hold.steps.push_back(step);
			}

			Note end = readNote(NoteType::HoldEnd, reader, version);
			end.ID = Note::getNextID();
			end.parentID = start.ID;
			score.notes[end.ID] = end;

			hold.end = end.ID;
			score.holdNotes[start.ID] = hold;
		}

		if (version.supportDamageNote())
		{
			reader.seek(damagesAddress);

			int damageCount = reader.readUInt32();
			score.notes.reserve(damageCount);
			for (int i = 0; i < damageCount; ++i)
			{
				Note note = readNote(NoteType::Damage, reader, version);
				note.ID = Note::getNextID();
				score.notes[note.ID] = note;
			}
		}

		if (version.supportLayers())
		{
			score.layers.clear();
			reader.seek(layersAddress);

			int layerCount = reader.readUInt32();
			score.layers.reserve(layerCount);
			for (int i = 0; i < layerCount; ++i)
			{
				std::string name = reader.readString();
				score.layers.push_back({ name });
			}
		}

		if (version.supportWaypoints())
		{
			score.waypoints.clear();
			reader.seek(waypointsAddress);

			int waypointCount = reader.readUInt32();
			score.waypoints.reserve(waypointCount);
			for (int i = 0; i < waypointCount; ++i)
			{
				std::string name = reader.readString();
				int tick = reader.readUInt32();
				score.waypoints.push_back({ name, tick });
			}
		}

		reader.close();
		return score;
	}

	void NativeScoreSerializer::serialize(const Score& score, std::string filename)
	{
		IO::BinaryWriter writer(filename);
		if (!writer.isStreamValid())
			return;

		// signature
		writer.writeString(UC_MMWS_SIGNATURE);

		// version
		writer.writeInt32(UC_MMWS_VERSION);

		// offsets address in order: metadata -> events -> taps -> holds
		// Cyanvas extension: -> damages -> layers -> waypoints
		uint32_t offsetsAddress = writer.getStreamPosition();
		writer.writeNull(sizeof(uint32_t) * 7);

		uint32_t metadataAddress = writer.getStreamPosition();
		writeMetadata(score.metadata, writer);

		uint32_t eventsAddress = writer.getStreamPosition();
		writeScoreEvents(score, writer);

		uint32_t tapsAddress = writer.getStreamPosition();
		writer.writeNull(sizeof(uint32_t));

		int noteCount = 0;
		for (const auto& [id, note] : score.notes)
		{
			if (note.getType() != NoteType::Tap)
				continue;

			writeNote(note, writer);
			++noteCount;
		}

		uint32_t holdsAddress = writer.getStreamPosition();

		// write taps count
		writer.seek(tapsAddress);
		writer.writeInt32(noteCount);

		writer.seek(holdsAddress);

		writer.writeInt32(score.holdNotes.size());
		for (const auto& [id, hold] : score.holdNotes)
		{
			unsigned int flags{};
			if (hold.startType == HoldNoteType::Guide)
				flags |= HOLD_GUIDE;
			if (hold.startType == HoldNoteType::Hidden)
				flags |= HOLD_START_HIDDEN;
			if (hold.endType == HoldNoteType::Hidden)
				flags |= HOLD_END_HIDDEN;
			if (hold.dummy)
				flags |= HOLD_DUMMY;
			writer.writeInt32(flags);

			// note data
			const Note& start = score.notes.at(hold.start.ID);
			writeNote(start, writer);
			writer.writeInt32((int)hold.start.ease);
			writer.writeInt32((int)hold.fadeType);
			writer.writeInt32((int)hold.guideColor);

			// steps
			int stepCount = hold.steps.size();
			writer.writeInt32(stepCount);
			for (const auto& step : hold.steps)
			{
				const Note& mid = score.notes.at(step.ID);
				writeNote(mid, writer);
				writer.writeInt32((int)step.type);
				writer.writeInt32((int)step.ease);
			}

			// end
			const Note& end = score.notes.at(hold.end);
			writeNote(end, writer);
		}

		uint32_t damagesAddress = writer.getStreamPosition();
		writer.writeNull(sizeof(uint32_t));

		// Cyanvas extension: write damages
		int damageNoteCount = 0;
		for (const auto& [id, note] : score.notes)
		{
			if (note.getType() != NoteType::Damage)
				continue;

			writeNote(note, writer);
			++damageNoteCount;
		}

		// Cyanvas extension: write layers
		uint32_t layersAddress = writer.getStreamPosition();

		// write damages count
		writer.seek(damagesAddress);
		writer.writeInt32(damageNoteCount);

		writer.seek(layersAddress);
		writer.writeInt32(score.layers.size());
		for (const auto& layer : score.layers)
		{
			writer.writeString(layer.name);
		}

		uint32_t waypointsAddress = writer.getStreamPosition();
		writer.writeInt32(score.waypoints.size());
		for (const auto& waypoint : score.waypoints)
		{
			writer.writeString(waypoint.name);
			writer.writeInt32(waypoint.tick);
		}

		// write offset addresses
		writer.seek(offsetsAddress);
		writer.writeInt32(metadataAddress);
		writer.writeInt32(eventsAddress);
		writer.writeInt32(tapsAddress);
		writer.writeInt32(holdsAddress);
		writer.writeInt32(damagesAddress);
		writer.writeInt32(layersAddress);
		writer.writeInt32(waypointsAddress);

		writer.flush();
		writer.close();
	}

	bool NativeScoreSerializer::canSerialize(const Score& score) { return true; }
}