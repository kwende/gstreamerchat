// ExampleChat.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include "CmdOptions.h"

int main(int argc, char** argv)
{
	auto options = CmdOptions::Parse(argc, argv); 
	if (options.IsValid)
	{

	}
}
