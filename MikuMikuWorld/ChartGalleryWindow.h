#pragma once
#include "Score.h"
#include "Rendering/Texture.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace MikuMikuWorld
{
	struct ChartState {
		bool isFavorite = false;
	};

	struct GalleryItem {
		std::string filepath;
		std::string filename;
		std::string extension; 
		std::string jacketPath;
		std::string title;
		std::string artist;    
		std::string author;    
		int totalCombo = 0;    
		std::string lengthStr = "-"; 

		std::shared_ptr<Texture> texture; 
		bool isFavorite = false;
	};

	class ChartGalleryWindow
	{
	private:
		bool recentLoaded = false;
		int activeTab = 0;
		char localSearchPath[512] = ""; 

		std::unordered_map<std::string, ChartState> galleryStates;
		std::vector<std::shared_ptr<GalleryItem>> recentItems;
		std::vector<std::shared_ptr<GalleryItem>> localItems;
		std::unique_ptr<Texture> defaultIcon = nullptr;

		std::vector<std::string> customFolders;
		
		std::string itemToDelete = "";
		bool openDeletePopup = false;

		char newFolderName[256] = "";
		bool openAddFolderPopup = false;

		void loadGalleryData();
		void saveGalleryData();
		std::shared_ptr<GalleryItem> loadItemInfo(const std::string& filepath);
		void drawGrid(std::vector<std::shared_ptr<GalleryItem>>& itemsToDraw, const char* gridId);
		void removeDeletedItemFromLists(const std::string& filepath);

	public:
		bool open = true;
		std::string pendingLoadScore = ""; 

		void update(const std::vector<std::string>& recentFiles);
	};
}