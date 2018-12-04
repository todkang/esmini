#include <iostream>
#include <string>
#include <random>

#include "ScenarioEngine.hpp"
#include "viewer.hpp"
#include "RoadManager.hpp"
#include "CommonMini.hpp"

#include <Windows.h>
#include <process.h>

double deltaSimTime;

static const double maxStepSize = 0.1;
static const double minStepSize = 0.01;
static const bool freerun = true;
static bool viewer_running = false;


static ScenarioEngine *scenarioEngine;

static HANDLE ghMutex;

void viewer_thread(void *data)
{
	// Create viewer
	osg::ArgumentParser *parser = (osg::ArgumentParser *)data;
	viewer::Viewer *viewer = new viewer::Viewer(roadmanager::Position::GetOpenDrive(), scenarioEngine->getSceneGraphFilename().c_str(), *parser);

	//  Create cars for visualization
	for (int i = 0; i < scenarioEngine->entities.object_.size(); i++)
	{
		viewer->AddCar(scenarioEngine->entities.object_[i]->model_id_);
	}

	while (!viewer->osgViewer_->done())
	{

		WaitForSingleObject(ghMutex, INFINITE);  // no time-out interval

		// Visualize cars
		for (int i = 0; i < scenarioEngine->entities.object_.size(); i++)
		{
			viewer::CarModel *car = viewer->cars_[i];
			roadmanager::Position pos = scenarioEngine->entities.object_[i]->pos_;

			car->SetPosition(pos.GetX(), pos.GetY(), pos.GetZ());
			car->SetRotation(pos.GetH(), pos.GetR(), pos.GetP());
		}

		ReleaseMutex(ghMutex);

		viewer->osgViewer_->frame();
		
		viewer_running = true;
	}

	delete viewer;

	viewer_running = false;
}

int main(int argc, char *argv[])
{	

	ghMutex = CreateMutex(
		NULL,              // default security attributes
		FALSE,             // initially not owned
		NULL);             // unnamed mutex

	if (ghMutex == NULL)
	{
		printf("CreateMutex error: %d\n", GetLastError());
		return 1;
	}

	// Simulation constants
	double endTime = 100;
	double simulationTime = 0;
	double timeStep = 1;

	// use an ArgumentParser object to manage the program arguments.
	osg::ArgumentParser arguments(&argc, argv);

	arguments.getApplicationUsage()->setApplicationName(arguments.getApplicationName());
	arguments.getApplicationUsage()->setDescription(arguments.getApplicationName());
	arguments.getApplicationUsage()->setCommandLineUsage(arguments.getApplicationName() + " [options]\n");
	arguments.getApplicationUsage()->addCommandLineOption("--osc <filename>", "OpenSCENARIO filename");
	arguments.getApplicationUsage()->addCommandLineOption("--ext_control <mode>", "Ego control (\"osc\", \"off\", \"on\")");

	if (arguments.argc() < 2)
	{
		arguments.getApplicationUsage()->write(std::cout, 1, 80, true);
		return -1;
	}

	std::string oscFilename;
	arguments.read("--osc", oscFilename);

	std::string ext_control_str;
	arguments.read("--ext_control", ext_control_str);

	ExternalControlMode ext_control;
	if (ext_control_str == "osc" || ext_control_str == "") ext_control = ExternalControlMode::EXT_CONTROL_BY_OSC;
	else if (ext_control_str == "off") ext_control = ExternalControlMode::EXT_CONTROL_OFF;
	else if (ext_control_str == "on") ext_control = ExternalControlMode::EXT_CONTROL_ON;
	else
	{
		LOG("Unrecognized external control mode: %s", ext_control_str.c_str());
		ext_control = ExternalControlMode::EXT_CONTROL_BY_OSC;
	}

	std::string record_filename;
	arguments.read("--record", record_filename);

	// Create scenario engine
	try 
	{ 
		scenarioEngine = new ScenarioEngine(oscFilename, simulationTime, ext_control);
	}
	catch (std::logic_error &e)
	{
		printf("%s\n", e.what());
		return -1;
	}

	// ScenarioGateway
	ScenarioGateway *scenarioGateway = scenarioEngine->getScenarioGateway();

	// Create a data file for later replay?
	if (!record_filename.empty())
	{
		LOG("Recording data to file %s", record_filename);
		scenarioGateway->RecordToFile(record_filename, scenarioEngine->getOdrFilename(), scenarioEngine->getSceneGraphFilename());
	}

	// Step scenario engine - zero time - just to reach init state	
	// Report all vehicles initially - to communicate initial position for external vehicles as well
	scenarioEngine->step(0.0, true);

	// Launch viewer in a separate thread
	HANDLE thread_handle = (HANDLE)_beginthread(viewer_thread, 0, &arguments);

	// Wait for the viewer to launch
	while (!viewer_running) SE_sleep(100);


	__int64 now, lastTimeStamp = 0;
	double simTime = 0;

	while (viewer_running)
	{
		// Get milliseconds since Jan 1 1970
		now = SE_getSystemTime();
		deltaSimTime = (now - lastTimeStamp) / 1000.0;  // step size in seconds
		lastTimeStamp = now;
		double adjust = 0;

		if (deltaSimTime > maxStepSize) // limit step size
		{
			adjust = -(deltaSimTime - maxStepSize);
		}
		else if (deltaSimTime < minStepSize)  // avoid CPU rush, sleep for a while
		{
			adjust = minStepSize - deltaSimTime;
			SE_sleep(adjust * 1000);
			lastTimeStamp += adjust * 1000;
		}

		deltaSimTime += adjust;

		// Time operations
		simTime = simTime + deltaSimTime;
		scenarioEngine->setSimulationTime(simTime);
		scenarioEngine->setTimeStep(deltaSimTime);

		// ScenarioEngine
		WaitForSingleObject(ghMutex, INFINITE);  // no time-out interval
		
		scenarioEngine->step(deltaSimTime);

		ReleaseMutex(ghMutex);
	}


	delete scenarioEngine;

	return 0;
}


