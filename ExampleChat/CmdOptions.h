#pragma once
#include <string>
#include "CLI/CLI.hpp"

class CmdOptions 
{
public:
	enum Mode {
		Server = 0,
		Client = 1,
	};

	std::string SourceIp; 
	std::string DestIp; 

	int SourcePort; 
	int DestPort; 
	bool IsServer; 

	bool IsValid; 

	static CmdOptions Parse(int argc, char** argv)
	{
		CLI::App app("Gstreamer audio chat app.");

		app.require_subcommand(1, 1); 

		CmdOptions ret = {};

		auto server = app.add_subcommand("server"); 
		server->add_option("--srcip", ret.SourceIp, "The local IP.")->required();
		server->add_option("--srcport", ret.SourcePort, "The local port #")->required();

		auto client = app.add_subcommand("client");
		client->add_option("--destip", ret.DestIp, "The destination IP.")->required();
		client->add_option("--destport", ret.DestPort, "The destination port #")->required();
		client->add_option("--srcip", ret.SourceIp, "The local IP.")->required();
		client->add_option("--srcport", ret.SourcePort, "The local port #")->required();
		
		try {
			app.parse(argc, argv);

			ret.IsServer = app.got_subcommand("server");
			ret.IsValid = true; 
		}
		catch (const CLI::ParseError& e) {
			std::cout << "Parameter error starting kStreamer: " << e.what() << std::endl;
			ret.IsValid = false; 
		}

		return ret; 
	}
};

