#pragma once

#include "ScoreSerializer.h"
#include "BinaryReader.h"
#include "BinaryWriter.h"

namespace MikuMikuWorld
{
	class NativeScoreSerializer : public ScoreSerializer
	{
	private:
		struct ScoreVersion;

		Note readNote(NoteType type, IO::BinaryReader& reader, const ScoreVersion& version);
		ScoreMetadata readMetadata(IO::BinaryReader& reader, const ScoreVersion& version);
		void readScoreEvents(Score& score, IO::BinaryReader& reader, const ScoreVersion& version);
		void writeNote(const Note& note, IO::BinaryWriter& writer);
		void writeMetadata(const ScoreMetadata& metadata, IO::BinaryWriter& writer);
		void writeScoreEvents(const Score& score, IO::BinaryWriter& writer);

	  public:
		void serialize(const Score& score, std::string filename) override;
		Score deserialize(std::string filename) override;

		static bool canSerialize(const Score& score);
	};
}