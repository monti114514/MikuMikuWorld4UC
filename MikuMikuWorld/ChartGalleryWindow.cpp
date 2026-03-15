#include "ChartGalleryWindow.h"
#include "Application.h"
#include "File.h"
#include "NativeScoreSerializer.h"
#include "Audio/Sound.h"
#include "UI.h"
#include "IconsFontAwesome5.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include "json.hpp" // JSONライブラリのパスに合わせてください

namespace MikuMikuWorld
{
	void ChartGalleryWindow::loadGalleryData()
	{
		std::string path = Application::getAppDir() + "gallery.json";
		std::ifstream file(IO::mbToWideStr(path)); 
		if (file.is_open()) {
			nlohmann::json j;
			try {
				file >> j;
				if (j.contains("charts")) {
					for (auto& [filepath, data] : j["charts"].items()) {
						galleryStates[filepath] = {
							data.value("isFavorite", false)
						};
					}
				}
				if (j.contains("folders")) {
					customFolders.clear();
					for (auto& folder : j["folders"]) {
						customFolders.push_back(folder.get<std::string>());
					}
				}
			} catch (...) {}
		}
	}

	void ChartGalleryWindow::saveGalleryData()
	{
		nlohmann::json j;
		for (const auto& [filepath, state] : galleryStates) {
			if (state.isFavorite) {
				j["charts"][filepath] = {
					{"isFavorite", state.isFavorite}
				};
			}
		}
		for (const auto& folder : customFolders) {
			j["folders"].push_back(folder);
		}
		std::string path = Application::getAppDir() + "gallery.json";
		std::ofstream file(IO::mbToWideStr(path));
		if (file.is_open()) {
			file << j.dump(4);
		}
	}

	std::shared_ptr<GalleryItem> ChartGalleryWindow::loadItemInfo(const std::string& filepath)
	{
		auto item = std::make_shared<GalleryItem>();
		item->filepath = filepath;
		item->filename = IO::File::getFilenameWithoutExtension(filepath);
		item->extension = IO::File::getFileExtension(filepath);
		item->title = "-"; 
		item->artist = "-";
		item->author = "-";

		if (galleryStates.find(filepath) != galleryStates.end()) {
			item->isFavorite = galleryStates[filepath].isFavorite;
		}

		try {
			Score tempScore = NativeScoreSerializer().deserialize(filepath);
			
			if (!tempScore.metadata.title.empty()) item->title = tempScore.metadata.title;
			if (!tempScore.metadata.artist.empty()) item->artist = tempScore.metadata.artist;
			if (!tempScore.metadata.author.empty()) item->author = tempScore.metadata.author;
			
			item->totalCombo = tempScore.notes.size();

			if (!tempScore.metadata.musicFile.empty()) {
				std::string absAudioPath = tempScore.metadata.musicFile;
				if (!IO::File::exists(absAudioPath)) {
					absAudioPath = IO::File::getFilepath(filepath) + tempScore.metadata.musicFile;
				}
				
				if (IO::File::exists(absAudioPath)) {
					Audio::SoundBuffer tempBuffer;
					if (Audio::decodeAudioFile(absAudioPath, tempBuffer).isOk()) {
						if (tempBuffer.sampleRate > 0) {
							double totalSeconds = static_cast<double>(tempBuffer.frameCount) / tempBuffer.sampleRate;
							
							int hours = static_cast<int>(totalSeconds / 3600);
							int minutes = static_cast<int>((totalSeconds - hours * 3600) / 60);
							double seconds = totalSeconds - hours * 3600 - minutes * 60;

							std::ostringstream timeStream;
							timeStream << std::setfill('0') << std::setw(2) << hours << ":"
							           << std::setfill('0') << std::setw(2) << minutes << ":"
							           << std::setfill('0') << std::fixed << std::setprecision(3) << seconds;
							item->lengthStr = timeStream.str();
						}
						tempBuffer.dispose();
					}
				}
			}

			item->jacketPath = tempScore.metadata.jacketFile;
			std::string absJacketPath = item->jacketPath;
			if (!absJacketPath.empty()) {
				if (!IO::File::exists(absJacketPath)) {
					absJacketPath = IO::File::getFilepath(filepath) + item->jacketPath;
				}
				if (IO::File::exists(absJacketPath)) {
					item->texture = std::make_shared<Texture>(absJacketPath);
				}
			}
		} catch (...) {}
		return item;
	}

	void ChartGalleryWindow::removeDeletedItemFromLists(const std::string& filepath)
	{
		auto removePredicate = [&](const std::shared_ptr<GalleryItem>& item) {
			return item->filepath == filepath;
		};
		recentItems.erase(std::remove_if(recentItems.begin(), recentItems.end(), removePredicate), recentItems.end());
		localItems.erase(std::remove_if(localItems.begin(), localItems.end(), removePredicate), localItems.end());
		galleryStates.erase(filepath);
		saveGalleryData();
	}

	void ChartGalleryWindow::drawGrid(std::vector<std::shared_ptr<GalleryItem>>& itemsToDraw, const char* gridId)
	{
		if (itemsToDraw.empty()) {
			ImGui::TextDisabled("  No charts to display.");
			return;
		}
	
		float windowVisibleX = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
		float cardWidth = 315.0f; 
		float cardHeight = 165.0f; 
		float spacing = 16.0f;
		float padding = 8.0f;
		float imageSize = 120.0f;
	
		for (int i = 0; i < itemsToDraw.size(); ++i)
		{
			auto& item = itemsToDraw[i];
			
			// IDをインデックスではなく「ファイルパス」にすることで、
			// リストが並び替わってもクリック判定が狂わないようにします。
			ImGui::PushID(item->filepath.c_str());
			
			ImGui::BeginGroup(); 
			ImVec2 cursorPos = ImGui::GetCursorScreenPos();
			ImDrawList* drawList = ImGui::GetWindowDrawList();
	
			// 1. 背景の描画（先に土台を描く）
			drawList->AddRectFilled(cursorPos, ImVec2(cursorPos.x + cardWidth, cursorPos.y + cardHeight), IM_COL32(35, 35, 35, 255), 5.0f);
	
			// 2. 画像の描画
			ImVec2 imgPos = ImVec2(cursorPos.x + padding, cursorPos.y + padding);
			if (item->texture) {
				drawList->AddImageRounded((void*)(intptr_t)item->texture->getID(), imgPos, ImVec2(imgPos.x + imageSize, imgPos.y + imageSize), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255), 4.0f);
			} else if (defaultIcon) {
				drawList->AddImageRounded((void*)(intptr_t)defaultIcon->getID(), imgPos, ImVec2(imgPos.x + imageSize, imgPos.y + imageSize), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255), 4.0f);
			} else {
				drawList->AddRectFilled(imgPos, ImVec2(imgPos.x + imageSize, imgPos.y + imageSize), IM_COL32(60, 60, 60, 255), 4.0f);
			}
	
			// 3. ハートボタン（お気に入り）
			// InvisibleButtonを消したので、このボタンが最前面で正しく反応するようになります。
			ImGui::SetCursorScreenPos(ImVec2(imgPos.x + 4, imgPos.y + 4));
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0.0f));
			ImGui::PushStyleColor(ImGuiCol_Text, item->isFavorite ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 0.8f));
			
			if (ImGui::Button(ICON_FA_HEART, ImVec2(24, 24))) {
				item->isFavorite = !item->isFavorite;
				galleryStates[item->filepath].isFavorite = item->isFavorite; 
				saveGalleryData(); 
			}
			bool favHovered = ImGui::IsItemHovered(); 
			ImGui::PopStyleColor(2);
	
			// 4. テキスト情報の描画
			float textStartX = imgPos.x + imageSize + padding + 15.0f;
			float textStartY = cursorPos.y + padding + 2.0f;
			float labelWidth = 65.0f;
			float valueStartX = textStartX + labelWidth + 12.0f;
	
			drawList->AddLine(ImVec2(textStartX + labelWidth, textStartY), ImVec2(textStartX + labelWidth, textStartY + 7 * 16.5f), IM_COL32(80, 80, 80, 255));
	
			const char* labels[] = { "File", "Format", "Title", "Artist", "Author", "Time", "Combo" };
			std::string values[] = {
				item->filename, item->extension, item->title, item->artist, item->author, item->lengthStr, std::to_string(item->totalCombo)
			};
	
			for (int row = 0; row < 7; ++row) {
				float rowY = textStartY + row * 16.5f;
				drawList->AddText(ImVec2(textStartX, rowY), IM_COL32(160, 160, 160, 255), labels[row]);
				
				ImVec2 clipMin(valueStartX, rowY);
				ImVec2 clipMax(cursorPos.x + cardWidth - padding, rowY + 16.0f);
				drawList->PushClipRect(clipMin, clipMax, true);
				drawList->AddText(clipMin, IM_COL32(230, 230, 230, 255), values[row].c_str());
				drawList->PopClipRect();
			}
	
			// 5. タイトルバーとタグの描画
			float bottomY = imgPos.y + imageSize + 6.0f;
			ImVec2 titleBarStart(imgPos.x, bottomY);
			ImVec2 titleBarEnd(imgPos.x + imageSize, bottomY + 22.0f);
			drawList->AddRectFilled(titleBarStart, titleBarEnd, IM_COL32(45, 45, 45, 255));
			
			ImVec2 titleSize = ImGui::CalcTextSize(item->title.c_str());
			float titleTextX = titleBarStart.x + (imageSize - titleSize.x) * 0.5f;
			if (titleTextX < titleBarStart.x + 4.0f) titleTextX = titleBarStart.x + 4.0f;
			drawList->PushClipRect(titleBarStart, titleBarEnd, true);
			drawList->AddText(ImVec2(titleTextX, titleBarStart.y + 4.0f), IM_COL32(180, 180, 180, 255), item->title.c_str());
			drawList->PopClipRect();
	
			ImGui::SetCursorScreenPos(ImVec2(titleBarEnd.x + 12.0f, bottomY));
			ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), ICON_FA_TAG " Personal");
	
			// 6. ゴミ箱ボタン
			ImGui::SetCursorScreenPos(ImVec2(cursorPos.x + cardWidth - padding - 24.0f, bottomY - 2.0f));
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
			if (ImGui::Button(ICON_FA_TRASH, ImVec2(24, 24))) {
				itemToDelete = item->filepath;
				openDeletePopup = true; 
			}
			bool trashHovered = ImGui::IsItemHovered(); 
			ImGui::PopStyleColor(2);
	
			ImGui::EndGroup(); 
	
			// --- ここからがカード全体の判定処理 ---
			
			// Group全体に対するホバーとクリックを判定します。
			bool cardHovered = ImGui::IsItemHovered();
			bool cardClicked = ImGui::IsItemClicked();
	
			// ホバー時は背景を少し明るくする（ハイライト）
			if (cardHovered) {
				drawList->AddRectFilled(cursorPos, ImVec2(cursorPos.x + cardWidth, cursorPos.y + cardHeight), IM_COL32(255, 255, 255, 15), 5.0f);
			}
	
			// カードがクリックされた際、ハートやゴミ箱の上でない場合のみ譜面を読み込む
			if (cardClicked && !favHovered && !trashHovered) {
				pendingLoadScore = item->filepath;
			}
	
			// 改行処理
			float lastGroupX = ImGui::GetItemRectMax().x;
			float nextGroupX = lastGroupX + spacing + cardWidth;
			if (i + 1 < itemsToDraw.size() && nextGroupX < windowVisibleX) {
				ImGui::SameLine(0, spacing);
			} else {
				ImGui::Dummy(ImVec2(0, 10.0f)); 
			}
			
			ImGui::PopID();
		}
	}

	void ChartGalleryWindow::update(const std::vector<std::string>& recentFiles)
	{
		if (!open) return;

		if (!recentLoaded)
		{
			loadGalleryData();

			for (const std::string& filepath : recentFiles)
			{
				if (!IO::File::exists(filepath)) continue;
				recentItems.push_back(loadItemInfo(filepath));
			}

			if (defaultIcon == nullptr)
			{
				std::string iconPath = Application::getAppDir() + "res\\textures\\gallery\\placeholder.png";
				if (IO::File::exists(iconPath))
				{
					defaultIcon = std::make_unique<Texture>(iconPath);
				}
			}

			recentLoaded = true;
		}

		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | 
		                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

		if (ImGui::Begin("GalleryScreen", &open, flags))
		{
			// ポップアップのトリガー処理
			if (openDeletePopup) {
				ImGui::OpenPopup("Delete Chart");
				openDeletePopup = false;
			}
			if (openAddFolderPopup) {
				ImGui::OpenPopup("Add Folder");
				openAddFolderPopup = false;
			}

			// 削除確認ポップアップの実装
			if (ImGui::BeginPopupModal("Delete Chart", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::Text("Are you sure you want to permanently delete this file?");
				ImGui::TextDisabled("%s", itemToDelete.c_str());
				ImGui::Separator();
				ImGui::Spacing();

				if (ImGui::Button("Delete", ImVec2(120, 0))) {
					try {
						std::filesystem::remove(IO::mbToWideStr(itemToDelete));
						removeDeletedItemFromLists(itemToDelete);
					} catch (...) {}
					ImGui::CloseCurrentPopup();
				}
				ImGui::SetItemDefaultFocus();
				ImGui::SameLine();
				if (ImGui::Button("Cancel", ImVec2(120, 0))) {
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			// フォルダ追加ポップアップの実装
			if (ImGui::BeginPopupModal("Add Folder", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::Text("Enter new folder name:");
				ImGui::InputText("##FolderName", newFolderName, sizeof(newFolderName));
				ImGui::Spacing();
				ImGui::Separator();

				if (ImGui::Button("Add", ImVec2(120, 0))) {
					if (strlen(newFolderName) > 0) {
						customFolders.push_back(std::string(newFolderName));
						saveGalleryData();
						memset(newFolderName, 0, sizeof(newFolderName)); // リセット
					}
					ImGui::CloseCurrentPopup();
				}
				ImGui::SetItemDefaultFocus();
				ImGui::SameLine();
				if (ImGui::Button("Cancel", ImVec2(120, 0))) {
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			if (ImGui::BeginTable("GalleryLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
			{
				ImGui::TableSetupColumn("Sidebar", ImGuiTableColumnFlags_WidthFixed, 200.0f);
				ImGui::TableSetupColumn("MainContent", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				if (ImGui::BeginChild("SidebarChild", ImVec2(0, 0), false))
				{
					ImGui::Spacing();
					
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0)); 
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));
					
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
					if (ImGui::Button(ICON_FA_PLUS, ImVec2(30, 30))) { 
						openAddFolderPopup = true; 
					}
					ImGui::SameLine();
					if (ImGui::Button(ICON_FA_TRASH, ImVec2(30, 30))) { /* 将来の拡張用 */ }
					ImGui::SameLine();
					if (ImGui::Button(ICON_FA_PEN, ImVec2(30, 30))) { /* 将来の拡張用 */ }
					
					ImGui::PopStyleColor(3);
					ImGui::Separator();
					ImGui::Spacing();

					if (ImGui::Selectable("  All", activeTab == 0)) activeTab = 0;
					if (ImGui::Selectable("  Recent", activeTab == 1)) activeTab = 1;
					if (ImGui::Selectable("  Favorites", activeTab == 2)) activeTab = 2;
					
					ImGui::Separator();
					ImGui::Spacing();



					if (ImGui::Selectable("  Team Projects", false)) {}
					if (ImGui::Selectable("  Personal", false)) {}

					// カスタムフォルダの描画
					for (const auto& folderName : customFolders) {
						std::string label = "  " + folderName;
						if (ImGui::Selectable(label.c_str(), false)) {}
					}

					
					ImGui::Spacing(); ImGui::Spacing();
					if (ImGui::Button("Create New Chart", ImVec2(-1, 40)))
					{
						open = false;
					}
				}
				ImGui::EndChild();

				ImGui::TableSetColumnIndex(1);
				if (ImGui::BeginChild("MainContentChild", ImVec2(0, 0), false))
				{
					std::vector<std::shared_ptr<GalleryItem>> recentToDraw;
					std::vector<std::shared_ptr<GalleryItem>> localToDraw;
					std::vector<std::shared_ptr<GalleryItem>> favoritesToDraw;

					for (auto& item : recentItems) {
						if (activeTab == 0 || activeTab == 1) recentToDraw.push_back(item);
						if (item->isFavorite) favoritesToDraw.push_back(item);
					}
					for (auto& item : localItems) {
						if (activeTab == 0) localToDraw.push_back(item);
						// 重複を避けてお気に入りに追加
						if (item->isFavorite && std::find_if(favoritesToDraw.begin(), favoritesToDraw.end(), [&](const std::shared_ptr<GalleryItem>& fav) { return fav->filepath == item->filepath; }) == favoritesToDraw.end()) {
							favoritesToDraw.push_back(item);
						}
					}

					if (activeTab == 0 || activeTab == 1)
					{
						ImGui::Spacing(); ImGui::Text("Recent Charts"); ImGui::Separator(); ImGui::Spacing();
						drawGrid(recentToDraw, "RecentGrid");
					}

					if (activeTab == 0)
					{
						ImGui::Spacing(); ImGui::Spacing(); ImGui::Spacing();
						ImGui::Text("All Charts in PC"); ImGui::Separator(); ImGui::Spacing();
						
						ImGui::SetNextItemWidth(400.0f);
						ImGui::InputText("##FolderPath", localSearchPath, sizeof(localSearchPath));
						ImGui::SameLine();

						if (ImGui::Button("Scan Folder"))
						{
							localItems.clear();
							try {
								std::wstring wSearchPath = IO::mbToWideStr(localSearchPath);
								std::filesystem::path searchPath(wSearchPath);

								if (std::filesystem::exists(searchPath) && std::filesystem::is_directory(searchPath))
								{
									for (const auto& entry : std::filesystem::recursive_directory_iterator(searchPath))
									{
										if (entry.is_regular_file())
										{
											std::string ext = entry.path().extension().string();
											if (ext == ".mmws" || ext == ".ccmmws" || ext == ".unchmmws")
											{
												std::string utf8Filepath = IO::wideStringToMb(entry.path().wstring());
												localItems.push_back(loadItemInfo(utf8Filepath));
											}
										}
									}
								}
							} catch (...) {}
						}
						ImGui::SameLine();
						if (ImGui::Button(ICON_FA_SEARCH " Search")) { }
						
						ImGui::TextDisabled("Enter parent folder path (e.g. C:\\Charts) and click Scan.");
						ImGui::Spacing(); ImGui::Spacing();
						
						drawGrid(localToDraw, "LocalGrid");
					}

					if (activeTab == 2)
					{
						ImGui::Spacing(); ImGui::Text("Favorites"); ImGui::Separator(); ImGui::Spacing();
						drawGrid(favoritesToDraw, "FavoritesGrid");
					}
				}
				ImGui::EndChild();
				
				ImGui::EndTable();
			}
		}
		ImGui::End();
	}
}