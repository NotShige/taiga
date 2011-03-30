/*
** Taiga, a lightweight client for MyAnimeList
** Copyright (C) 2010-2011, Eren Okka
** 
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
** 
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
** 
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "std.h"
#include "animelist.h"
#include "common.h"
#include "media.h"
#include "myanimelist.h"
#include "recognition.h"
#include "resource.h"
#include "string.h"

CRecognition Meow;

class CToken {
public:
  CToken() : Encloser('\0'), Separator('\0'), Virgin(true) {}
  wchar_t Encloser, Separator;
  wstring Content;
  bool Virgin;
};

// =============================================================================

CRecognition::CRecognition() {
  // Load keywords into memory
  ReadKeyword(IDS_KEYWORD_AUDIO, AudioKeywords);
  ReadKeyword(IDS_KEYWORD_VIDEO, VideoKeywords);
  ReadKeyword(IDS_KEYWORD_EXTRA, ExtraKeywords);
  ReadKeyword(IDS_KEYWORD_EXTRA_UNSAFE, ExtraUnsafeKeywords);
  ReadKeyword(IDS_KEYWORD_VERSION, VersionKeywords);
  ReadKeyword(IDS_KEYWORD_EXTENSION, ValidExtensions);
  ReadKeyword(IDS_KEYWORD_EPISODE, EpisodeKeywords);
  ReadKeyword(IDS_KEYWORD_EPISODE_PREFIX, EpisodePrefixes);
}

// =============================================================================

bool CRecognition::CompareEpisode(CEpisode& episode, const CAnime& anime, 
                                  bool strict, bool check_episode, bool check_date) {
  // Leave if title is empty
  if (episode.Title.empty()) return false;
  // Leave if episode number is out of range
  if (check_episode && anime.Series_Episodes > 1) {
    int number = GetEpisodeHigh(episode.Number);
    if (number == 0 || number > anime.Series_Episodes) return false;
  }
  // Leave if not yet aired
  if (check_date && !anime.IsAiredYet()) return false;

  // Remove unnecessary characters
  wstring title = episode.Title;
  CleanTitle(title);
  if (title.empty()) return false;

  // Compare with main title then synonyms
  wstring anime_title = anime.Series_Title;
  if (CompareTitle(title, anime_title, episode, anime, strict) ||
      CompareSynonyms(title, anime_title, anime.Series_Synonyms, episode, anime, strict) ||
      CompareSynonyms(title, anime_title, anime.Synonyms, episode, anime, strict)) {
        // Assume episode 1 if matched one-episode series
        if (episode.Number.empty() && anime.Series_Episodes == 1) episode.Number = L"1";
        return true;
  }

  // Failed
  return false;
}

bool CRecognition::CompareTitle(const wstring& title, wstring& anime_title, 
                                CEpisode& episode, const CAnime& anime, bool strict) {
  // Remove unnecessary characters
  if (anime_title.empty()) return false;
  CleanTitle(anime_title);
  if (anime_title.empty()) return false;
  
  // Compare with title + number
  if (strict && anime.Series_Episodes == 1 && !episode.Number.empty()) {
    if (IsEqual(title + episode.Number, anime_title)) {
      episode.Title += episode.Number;
      episode.Number.clear();
      return true;
    }
  }
  // Compare with title
  if (strict) {
    if (IsEqual(title, anime_title)) return true;
  } else {
    if (InStr(title, anime_title, 0, true) > -1) return true;
  }

  // Failed
  return false;
}

bool CRecognition::CompareSynonyms(const wstring& title, wstring& anime_title, const wstring& synonyms, 
                                   CEpisode& episode, const CAnime& anime, bool strict) {
  size_t index_begin = 0, index_end;
  do {
    index_end = synonyms.find(L"; ", index_begin);
    if (index_end == wstring::npos) index_end = synonyms.length();
    anime_title = synonyms.substr(index_begin, index_end - index_begin);
    if (CompareTitle(title, anime_title, episode, anime, strict)) return true;
    index_begin = index_end + 2;
  } while (index_begin <= synonyms.length());
  return false;
}

// =============================================================================

bool CRecognition::ExamineTitle(wstring title, CEpisode& episode, 
                                bool examine_inside, bool examine_outside, bool examine_number,
                                bool check_extras, bool check_extension) {
  // Clear previous data
  episode.Clear();

  // Remove zero width space character
  Erase(title, L"\u200B");
                  
  // Trim media player name
  MediaPlayers.EditTitle(title);
  if (title.empty()) return false;

  // Retrieve file name from full path
  if (title.length() > 2 && title.at(1) == ':' && title.at(2) == '\\') {
    episode.Folder = GetPathOnly(title);
    title = GetFileName(title);
  }
  episode.File = title;

  // Check and trim file extension
  wstring extension = GetFileExtension(title);
  if (extension.length() < title.length() && extension.length() <= 5) {
    if (IsAlphanumeric(extension) && CheckFileExtension(extension, ValidExtensions)) {
      episode.Format = ToUpper_Copy(extension);
      title.resize(title.length() - extension.length() - 1);
    } else {
      if (check_extension) return false;
    }
  }

  // Tokenize
  vector<CToken> tokens;
  tokens.reserve(4);
  TokenizeTitle(title, L"[](){}", tokens);
  if (tokens.empty()) return false;
  title.clear();

  // Examine tokens
  for (unsigned int i = 0; i < tokens.size(); i++) {
    if (IsTokenEnclosed(tokens[i])) {
      if (examine_inside) ExamineToken(tokens[i], episode, check_extras);
    } else {
      if (examine_outside) ExamineToken(tokens[i], episode, check_extras);
    }
  }

  // Tidy up tokens
  for (unsigned int i = 1; i < tokens.size() - 1; i++) {
    if (tokens[i - 1].Virgin == false || tokens[i].Virgin == false || 
      IsTokenEnclosed(tokens[i - 1]) == true || tokens[i].Encloser != '(' || 
      tokens[i - 1].Content.length() < 2) {
        continue;
    }
    tokens[i - 1].Content += L"(" + tokens[i].Content + L")";
    if (IsTokenEnclosed(tokens[i + 1]) == false) {
      tokens[i - 1].Content += tokens[i + 1].Content;
      if (tokens[i - 1].Separator == '\0')
        tokens[i - 1].Separator = tokens[i + 1].Separator;
      tokens.erase(tokens.begin() + i + 1);
    }
    tokens.erase(tokens.begin() + i);
    i = 0;
  }
  for (unsigned int i = 0; i < tokens.size(); i++) {
    wchar_t trim_char[] = {tokens[i].Separator, '\0'};
    Trim(tokens[i].Content, trim_char);
    if (tokens[i].Content.length() < 2) {
      tokens.erase(tokens.begin() + i);
      i--;
    }
  }

  // Get group name and title
  int group_index = -1, title_index = -1;
  vector<int> group_vector, title_vector;
  for (unsigned int i = 0; i < tokens.size(); i++) {
    if (IsTokenEnclosed(tokens[i])) {
      group_vector.push_back(i);
    } else {
      title_vector.push_back(i);
    }
  }
  if (title_vector.size() > 0) {
    title_index = title_vector[0];
    title_vector.erase(title_vector.begin());
  } else {
    if (group_vector.size() > 1) {
      title_index = group_vector[1];
      group_vector.erase(group_vector.begin() + 1);
    } else if (group_vector.size() > 0) {
      title_index = group_vector[0];
      group_vector.erase(group_vector.begin());
    }
  }
  for (unsigned int i = 0; i < group_vector.size(); i++) {
    if (tokens[group_vector[i]].Virgin) {
      group_index = group_vector[i];
      group_vector.erase(group_vector.begin() + i);
      break;
    }
  }
  if (group_index == -1) {
    if (title_vector.size() > 0) {
      group_index = title_vector.back();
      title_vector.erase(title_vector.end() - 1);
    }
  }
  if (title_index > -1) {
    ReplaceChar(tokens[title_index].Content, tokens[title_index].Separator, ' ');
    Trim(tokens[title_index].Content, L" -");
    title = tokens[title_index].Content;
    tokens[title_index].Content.clear();
  }
  if (group_index > -1) {
    if (!IsTokenEnclosed(tokens[group_index])) {
      ReplaceChar(tokens[group_index].Content, tokens[group_index].Separator, ' ');
      Trim(tokens[group_index].Content, L" -");
    }
    episode.Group = tokens[group_index].Content;
    tokens[group_index].Content.clear();
  }

  // Get episode number and version
  if (examine_number && episode.Number.empty()) {
    for (int i = title.length() - 1; i >= 0; i--) {
      if (IsNumeric(title[i])) {
        episode.Number = title[i] + episode.Number;
      } else {
        if (episode.Number.empty()) continue;
        switch (title[i]) {
          case '-': case '&':
            episode.Number = L"-" + episode.Number;
            break;
          case 'v': case 'V':
            episode.Version = episode.Number;
            episode.Number.clear();
            break;
          case '(': case ')':
            episode.Number.clear();
            i = 0;
            break;
          case '.':
            i = InStrRev(title, L" ", i);
            episode.Number.clear();
            break;
          case ' ': { // TODO case tokens[title_index].Separator:
            if (!ValidateEpisodeNumber(episode)) break;
            // Break title into two parts
            wstring str_left = title.substr(0, i + 1);
            wstring str_right = title.substr(i + 1 + episode.Number.length());
            // Tidy up strings
            Replace(str_left, L"  ", L" ", true);
            Replace(str_right, L"  ", L" ", true);
            TrimLeft(episode.Number, L"-");
            TrimRight(str_left);
            TrimLeft(str_right);
            TrimEpisodeWord(str_left, true);
            TrimRight(str_left, L" -");
            TrimLeft(str_right, L" -");
            // Return title and name
            title = str_left;
            if (str_right.length() > 2) {
              episode.Name = str_right;
            }
            i = 0;
            break;
          }
          default:
            episode.Number.clear();
            break;
        }
      }
    }
  }

  // Examine remaining tokens once more
  for (unsigned int i = 0; i < tokens.size(); i++) {
    if (!tokens[i].Content.empty()) {
      ExamineToken(tokens[i], episode, true);
    }
  }

  // Episode number and title can't be the same
  if (!episode.Number.empty() && !episode.Group.empty()) {
    if (IsNumeric(title) && IsEqual(title, episode.Number)) {
      title = episode.Group;
      episode.Group.clear();
    }
  }

  // Set the final title, hopefully name of the anime
  episode.Title = title;
  return !title.empty();
}

// =============================================================================

void CRecognition::ExamineToken(CToken& token, CEpisode& episode, bool compare_extras) {
  // Split into words
  // The most common non-alphanumeric character is the separator
  vector<wstring> words;
  token.Separator = GetMostCommonCharacter(token.Content);
  Split(token.Content, wstring(1, token.Separator), words);
  
  // Revert if there are words that are too short
  // This prevents splitting group names like "m.3.3.w" and keywords like "H.264"
  if (IsTokenEnclosed(token)) {
    for (unsigned int i = 0; i < words.size(); i++) {
      if (words[i].length() == 1) {
        words.clear(); words.push_back(token.Content); break;
      }
    }
  }

  // Compare with keywords
  for (unsigned int i = 0; i < words.size(); i++) {
    if (words[i].empty()) continue;
    #define RemoveWordFromToken(b) { \
      Erase(token.Content, words[i], b); token.Virgin = false; }
    
    // Checksum
    if (episode.Checksum.empty() && words[i].length() == 8 && IsHex(words[i])) {
      episode.Checksum = words[i];
      RemoveWordFromToken(false);
    // Video resolution
    } else if (episode.Resolution.empty() && IsResolution(words[i])) {
      episode.Resolution = words[i];
      RemoveWordFromToken(false);
    // Video info
    } else if (CompareKeys(words[i], Meow.VideoKeywords)) {
      AppendKeyword(episode.VideoType, words[i]);
      RemoveWordFromToken(true);
    // Audio info
    } else if (CompareKeys(words[i], Meow.AudioKeywords)) {
      AppendKeyword(episode.AudioType, words[i]);
      RemoveWordFromToken(true);
    // Version
    } else if (episode.Version.empty() && CompareKeys(words[i], Meow.VersionKeywords)) {
      episode.Version.push_back(words[i].at(words[i].length() - 1));
      RemoveWordFromToken(true);
    // Episode number
    } else if (episode.Number.empty() && IsEpisodeFormat(words[i], episode)) {
      for (unsigned int j = i + 1; j < words.size(); j++)
        if (!words[j].empty()) episode.Name += (j == i + 1 ? L"" : L" ") + words[j];
      token.Content.resize(InStr(token.Content, words[i], 0));
    } else if (episode.Number.empty() && (i == 0 || i == words.size() - 1) && IsNumeric(words[i])) {
      episode.Number = words[i];
      if (!ValidateEpisodeNumber(episode)) continue;
      if (i > 0 && CompareKeys(words[i - 1], Meow.EpisodeKeywords)) Erase(token.Content, words[i - 1], true);
      RemoveWordFromToken(false);
    // Extras
    } else if (compare_extras && CompareKeys(words[i], Meow.ExtraKeywords)) {
      AppendKeyword(episode.Extra, words[i]);
      RemoveWordFromToken(true);
    } else if (compare_extras && CompareKeys(words[i], Meow.ExtraUnsafeKeywords)) {
      AppendKeyword(episode.Extra, words[i]);
      if (IsTokenEnclosed(token)) RemoveWordFromToken(true);
    }

    #undef RemoveWordFromToken
  }
}

// =============================================================================

// Helper functions

void CRecognition::AppendKeyword(wstring& str, const wstring& keyword) {
  if (!str.empty()) str.append(L" ");
  str.append(keyword);
}

bool CRecognition::CompareKeys(const wstring& str, const vector<wstring>& keys) {
  if (!str.empty())
    for (unsigned int i = 0; i < keys.size(); i++)
      if (IsEqual(str, keys[i]))
        return true;
  return false;
}

void CRecognition::CleanTitle(wstring& title) {
  EraseUnnecessary(title);
  TransliterateSpecial(title);
  ErasePunctuation(title, true);
}

void CRecognition::EraseUnnecessary(wstring& str) {
  Erase(str, L"The ", true);
  Erase(str, L" The", true);
}

void CRecognition::TransliterateSpecial(wstring& str) {
  ReplaceChar(str, L'\u00E9', L'e'); // small e acute accent
  ReplaceChar(str, L'\uFF0F', L'/'); // unicode slash
  ReplaceChar(str, L'\uFF5E', L'~'); // unicode tilde
  ReplaceChar(str, L'\u223C', L'~'); // unicode tilde 2
  ReplaceChar(str, L'\u301C', L'~'); // unicode tilde 3
  ReplaceChar(str, L'\uFF1F', L'?'); // unicode question mark
  ReplaceChar(str, L'\uFF01', L'!'); // unicode exclamation point
  
  Replace(str, L" & ", L" and ", true, false); // & and 'and' are equivalent
}

bool CRecognition::IsEpisodeFormat(const wstring& str, CEpisode& episode) {
  unsigned int numstart, i, j;

  // Find first number
  for (numstart = 0; numstart < str.length() && !IsNumeric(str.at(numstart)); numstart++);
  if (numstart == str.length()) return false;

  // Check for episode prefix
  if (numstart != 0) {
    if (!CompareKeys(str.substr(0,numstart),Meow.EpisodePrefixes)) return false;
  }

  for (i = numstart; i < str.length(); i++) {
    if (!IsNumeric(str.at(i))) {
      // ##-##
      if (i - numstart > 1 && (str.at(i) == '-' || str.at(i) == '&')) {
        if (i == str.length() - 1 || !IsNumeric(str.at(i + 1))) return false;
        for(j = i + 1; j < str.length() && IsNumeric(str.at(j)); j++);
        if (ToINT(str.substr(numstart, i-numstart)) + 1 == ToINT(str.substr(i + 1, j - 1 - i))) {
          episode.Number = str.substr(numstart,j - numstart);
          // ##-##v#
          if(j < str.length() - 1 && (str.at(j) == 'v' || str.at(j) =='V')) {
            numstart = i + 1;
            continue;
          } else return true;
        } else {
          episode.Number.clear();
          return false;
        }
      } 
      // ##v#
      else if ((str.at(i) == 'v' || str.at(i) == 'V') && i - numstart > 1) {
        if (i == str.length() - 1 || !IsNumeric(str.at(i + 1))) return false;
        if (episode.Number.empty()) episode.Number = str.substr(numstart, i - numstart);
        episode.Version = str.substr(i + 1);
        return true;
      } else return false;
    }
  }

  // EP.##
  if (numstart != 0) {
    episode.Number = str.substr(numstart,str.length() - numstart);
    if (!ValidateEpisodeNumber(episode)) return false;
    return true;
  }

  // ##
  return false;
}

bool CRecognition::IsResolution(const wstring& str) {
  // *###x###*
  if (str.length() > 6) {
    int pos = InStr(str, L"x", 0);
    if (pos > -1) {
      for (unsigned int i = 0; i < str.length(); i++) {
        if (i != pos && !IsNumeric(str.at(i))) return false;
      }
      return true;
    }
  // *###p
  } else if (str.length() > 3) {
    if (str.at(str.length() - 1) == 'p') {
      for (unsigned int i = 0; i < str.length() - 1; i++) {
        if (!IsNumeric(str.at(i))) return false;
      }
      return true;
    }
  }
  return false;
}

bool CRecognition::IsTokenEnclosed(const CToken& token) {
  return token.Encloser == '[' || token.Encloser == '(' || token.Encloser == '{';
}

void CRecognition::ReadKeyword(unsigned int uID, vector<wstring>& str) {
  wstring str_buff; 
  ReadStringTable(uID, str_buff); 
  Split(str_buff, L", ", str);
}

size_t CRecognition::TokenizeTitle(const wstring& str, const wstring& delimiters, vector<CToken>& tokens) {
  size_t index_begin = str.find_first_not_of(delimiters);
  while (index_begin != wstring::npos) {
    size_t index_end = str.find_first_of(delimiters, index_begin + 1);
    tokens.resize(tokens.size() + 1);
    if (index_end == wstring::npos) {
      tokens.back().Content = str.substr(index_begin);
      break;
    } else {
      tokens.back().Content = str.substr(index_begin, index_end - index_begin);
      if (index_begin > 0) tokens.back().Encloser = str.at(index_begin - 1);
      index_begin = str.find_first_not_of(delimiters, index_end + 1);
    }
  }
  return tokens.size();
}

void CRecognition::TrimEpisodeWord(wstring& str, bool erase_rightleft) {
    if (erase_rightleft) {
      for (unsigned int j = 0; j < Meow.EpisodeKeywords.size(); j++)
        EraseRight(str, L" " + Meow.EpisodeKeywords[j], true);
    } else {
      for (unsigned int j = 0; j < Meow.EpisodeKeywords.size(); j++)
        EraseLeft(str, Meow.EpisodeKeywords[j] + L" ", true);
    }
}

bool CRecognition::ValidateEpisodeNumber(CEpisode& episode) {
  int number = ToINT(episode.Number);
  if (number <= 0 || number > 1000) {
    if (number > 1950 && number < 2050) {
      AppendKeyword(episode.Extra, L"Year: " + episode.Number);
    }
    episode.Number.clear();
    return false;
  }
  return true;
}