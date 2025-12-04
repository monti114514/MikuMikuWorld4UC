#pragma once
#include <chrono>
#include <string>
#include <vector>
#include <numeric>

namespace IO
{
	class File
	{
	  private:
		FILE* stream;
		std::string filename;

	  public:
		File(const std::wstring& filename, const wchar_t* mode);
		File(const std::string& filename, const char* mode);
		File();
		~File();

		static std::string getFilename(const std::string& filename);
		static std::string getFileExtension(const std::string& filename);
		static std::string getFilenameWithoutExtension(const std::string& filename);
		static std::string getFilepath(const std::string& filename);
		static std::string fixPath(const std::string& path);
		static bool exists(const std::string& path);
		static bool exists(const std::wstring& path);

		void open(const std::wstring& filename, const wchar_t* mode);
		void open(const std::string& filename, const char* mode);
		std::chrono::time_point<std::chrono::system_clock> getLastWriteTime() const;
		void close();
		void flush();

		std::vector<uint8_t> readAllBytes();
		std::string readLine() const;
		std::vector<std::string> readAllLines() const;
		std::string readAllText() const;
		void write(const std::string& str);
		void writeLine(const std::string line);
		void writeAllLines(const std::vector<std::string>& lines);
		void writeAllBytes(const std::vector<uint8_t>& bytes);
		bool isEndofFile() const;
	};

	enum class FileDialogResult : uint8_t
	{
		Error,
		Cancel,
		OK
	};

	enum class DialogType : uint8_t
	{
		Open,
		Save
	};

	enum class DialogSelectType : uint8_t
	{
		File,
		Folder
	};

	struct FileDialogFilter
	{
		std::string filterName;
		std::string filterType;
	};

	class FileDialog
	{
	  private:
		FileDialogResult showFileDialog(DialogType type, DialogSelectType selectType);

	  public:
		std::string title;
		std::vector<FileDialogFilter> filters;
		std::string inputFilename;
		std::string outputFilename;
		std::string defaultExtension;
		uint32_t filterIndex = 0;
		void* parentWindowHandle = nullptr;

		FileDialogResult openFile();
		FileDialogResult saveFile();
	};

	inline FileDialogFilter combineFilters(const std::string& filterName,
	                                       const std::initializer_list<FileDialogFilter>& filters)
	{
		std::string filterType;
		if (!filters.size())
			return { filterName, "*.*" };
		filterType.reserve(std::accumulate(filters.begin(), filters.end(), size_t(0),
		                                   [](size_t sz, const FileDialogFilter& filter)
		                                   { return sz + filter.filterType.size() + 1; }) -
		                   1);
		auto begin = filters.begin(), end = filters.end();
		filterType += (begin++)->filterType;
		for (; begin != end; ++begin)
			filterType.append(";").append(begin->filterType);
		return { filterName, filterType };
	}

	extern FileDialogFilter mmwsFilter;
	extern FileDialogFilter susFilter;
	extern FileDialogFilter uscFilter;
	extern FileDialogFilter lvlDatFilter;
	extern FileDialogFilter imageFilter;
	extern FileDialogFilter audioFilter;
	extern FileDialogFilter presetFilter;
	extern FileDialogFilter allFilter;
}
