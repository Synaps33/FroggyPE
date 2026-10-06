#ifndef NET_MINECRAFT_CLIENT__OptionsFile_H__
#define NET_MINECRAFT_CLIENT__OptionsFile_H__

//package net.minecraft.client;
#include <string>
#include <vector>
typedef std::vector<std::string> StringVector;
class OptionsFile
{
public:
	OptionsFile();
	explicit OptionsFile(const std::string& directory);
	/*
	 * Points the file at <directory>/options.txt.
	 *
	 * A bare relative "options.txt" is not usable on the SF2000/GB300: the
	 * frontend stubs getcwd() to return NULL, so the process working directory is
	 * undefined and the settings ended up written somewhere unreachable. Nothing
	 * persisted -- render distance, auto jump and the rest were silently lost on
	 * every restart.
	 */
	void setDirectory(const std::string& directory);
    void save(const StringVector& settings);
	StringVector getOptionStrings();
	const std::string& getPath() const { return settingsPath; }

private:
	void rebuildPath();
	std::string directory;
	std::string settingsPath;
};

#endif /* NET_MINECRAFT_CLIENT__OptionsFile_H__ */
