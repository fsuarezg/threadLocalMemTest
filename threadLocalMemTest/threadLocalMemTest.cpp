#include <RixSampleFilter.h>
#include <string>
#include <iostream>
#include <fstream>
#include <vector>
//maybe?
#include <thread>
#include <sstream>

using namespace std;

int maxStoredBuckets = 2; // max # buckets stored in memory 
int thread_local test[2] = { -1 , -1 };


class threadLocalMemTest : public RixSampleFilter {
public:
	int threadCount;
	
	threadLocalMemTest() {};
	virtual ~threadLocalMemTest() {};

	virtual int Init(RixContext& ctx, char const* pluginPath);
	virtual RixSCParamInfo const* GetParamTable();
	virtual void Finalize(RixContext& ctx);

	virtual void Filter(RixSampleFilterContext& fCtx, RtConstPointer instance);

	void getpixCoordinates(int sample, const RtPoint2 * screen, const RtFloat * screenwindow, double pixelWidth, double pixelHeight, int frameWidth, int frameHeight, RtFloat bucketsize[], int currentBucket[], int pixCoordinates[]);
	void print(string toPrint);
	void printAlways(string toPrint);

	virtual int CreateInstanceData(RixContext& ctx, char const* handle, RixParameterList const* params, InstanceData* instance);

private:
	RixMutex	*threadCountMutex;
	RixMutex	*generalMutex;

};

int threadLocalMemTest::Init(RixContext &ctx, char const *pluginpath)
{
	RixContext* context = RixGetContext();
	RixRenderState* renderState = (RixRenderState*)context->GetRixInterface(k_RixRenderState);
	RixThreadUtils *threadUtils = (RixThreadUtils*)ctx.GetRixInterface(k_RixThreadUtils);
	RixRenderState::FrameInfo frameInf;
	renderState->GetFrameInfo(&frameInf);
	int frameWidth = frameInf.displayState.resolution[0];	//get full render image width
	int frameHeight = frameInf.displayState.resolution[1];	//get full render image height

	if (RixThreadUtils *threadUtils = (RixThreadUtils*)ctx.GetRixInterface(k_RixThreadUtils))
	{
		generalMutex = threadUtils->NewMutex();
	}

	threadCount = 0;

	

	return 0;
}

RixSCParamInfo const *threadLocalMemTest::GetParamTable()
{
	static RixSCParamInfo table[] =
	{
		//End of table
		RixSCParamInfo()
	};

	return &table[0];
}

void threadLocalMemTest::Filter(RixSampleFilterContext& fCtx, RtConstPointer instance)
{	// GET RENDERMAN INTERFACES
	RixContext* context = RixGetContext();
	RixRenderState* renderState = (RixRenderState*)context->GetRixInterface(k_RixRenderState);
	RixRenderState::FrameInfo frameInf;
	renderState->GetFrameInfo(&frameInf);
	int frameWidth = frameInf.displayState.resolution[0];	//get full render image width
	int frameHeight = frameInf.displayState.resolution[1];	//get full render image height

	// GET OPTIONS
	int threadId;
	RixRenderState::Type type;
	RtInt count;
	RtFloat bucketsize[2];
	renderState->GetOption("limits:bucketsize", &bucketsize, sizeof(RtFloat[2]), &type, &count);	//get bucketsize
	RtFloat formatres[2];
	renderState->GetOption("Ri:FormatResolution", &formatres, sizeof(RtFloat[2]), &type, &count);	//get format resolution
	RtFloat screenwindow[4];
	renderState->GetOption("Ri:ScreenWindow", &screenwindow, sizeof(RtFloat[4]), &type, &count);	//get screen window [left, right, bottom, top]
	RtFloat cropwindow[4];
	renderState->GetOption("Ri:CropWindow", &cropwindow, sizeof(RtFloat[4]), &type, &count);		//get crop window 
	RtFloat maxsamples;
	renderState->GetOption("RiHider:maxsamples", &maxsamples, sizeof(RtFloat), &type, &count);		//get max number of samples taken for a pixel
	RtFloat filtersize[2];
	renderState->GetOption("RiPixelFilter:width", &filtersize, sizeof(RtFloat[2]), &type, &count);
	filtersize[0] = 0;
	filtersize[1] = 0;
	int bucketshift = floor(filtersize[0] / 2); // the AA filter shifts buckets a certain amount depending on it size
	double pixelWidth = 1 / formatres[0];
	double pixelHeight = 1 / formatres[1];
	RtPoint2 const* screen = fCtx.screen;
	RtRayGeometry const* rays = fCtx.rays;

	// FIND WHICH BUCKET WE ARE CURRENTLY WORKING ON, DEFINED BY THE LOWEST (x,y) PIXEL COORDINATES OF THE BUCKET
	int currentBucket[2] = { frameWidth , frameHeight }; //(x,y)
	RixShadingContext const &shading = *fCtx.shadeCtxs[0];
	int point = 0;
	int sampleNr = shading.integratorCtxIndex[point];
	int pixCoordinates[2] = { 0,0 }; //(x,y)
	getpixCoordinates(sampleNr, screen, screenwindow, pixelWidth, pixelHeight, frameWidth, frameHeight, bucketsize, currentBucket, pixCoordinates);
	currentBucket[0] = (floor((pixCoordinates[0] + bucketshift) / (int)bucketsize[0]) * (int)bucketsize[0]) - bucketshift;
	currentBucket[1] = (floor((pixCoordinates[1] + bucketshift) / (int)bucketsize[1]) * (int)bucketsize[1]) - bucketshift;

	if (test[0] == -1 && test[1] == -1) {
		test[0] = currentBucket[0];
		test[1] = currentBucket[1];
		printAlways("initialized: " + to_string(test[0]) + " " + to_string(test[1]));
	}
	std::ostringstream ss;
	ss << std::this_thread::get_id();
	std::string idstr = ss.str();
	printAlways("thread "+ idstr + " working on bucket: " + std::to_string(currentBucket[0]) + " , " + std::to_string(currentBucket[1]));
	printAlways("thread " + idstr + " previous bucket: " + std::to_string(test[0]) + " , " + std::to_string(test[1]));
		
}

void  threadLocalMemTest::getpixCoordinates(int sample, const RtPoint2 * screen, const RtFloat * screenwindow, double pixelWidth, double pixelHeight, int frameWidth, int frameHeight, RtFloat bucketsize[], int currentBucket[], int pixCoordinates[]) {

	float sampleScreenXcoordinate = screen[sample].x; //get the x screen coordinate of sample
	sampleScreenXcoordinate = (sampleScreenXcoordinate + abs(screenwindow[1])); // shift range to positive values, between 0 and 2*screenwindow[1]
	sampleScreenXcoordinate /= (2 * abs(screenwindow[1])); //normalize
	sampleScreenXcoordinate /= pixelWidth;
	sampleScreenXcoordinate = floor(sampleScreenXcoordinate);

	float sampleScreenYcoordinate = screen[sample].y * -1; //get the y screen coordinate of sample, multiply by -1 to invert in correct direction
	sampleScreenYcoordinate = (sampleScreenYcoordinate + abs(screenwindow[2])); // shift range to positive values, between 0 and 2*screenwindow[2]
	sampleScreenYcoordinate /= (2 * abs(screenwindow[2])); //normalize
	sampleScreenYcoordinate /= pixelHeight;
	sampleScreenYcoordinate = floor(sampleScreenYcoordinate);

	//The following if tests were added for the case when samples are taken exactly on the lower and right edge of a bucket, this hapens rarely
	if (sampleScreenYcoordinate == (currentBucket[1] + bucketsize[1])) {
		sampleScreenYcoordinate -= 1;
	}
	if (sampleScreenXcoordinate == (currentBucket[0] + bucketsize[0])) {
		sampleScreenXcoordinate -= 1;
	}

	pixCoordinates[0] = sampleScreenXcoordinate;
	pixCoordinates[1] = sampleScreenYcoordinate;

}

void  threadLocalMemTest::print(string toPrint) {
	RixContext* context = RixGetContext();
	RixMessages* msgs = (RixMessages*)context->GetRixInterface(k_RixMessages);
	const char * constX = toPrint.c_str();
	msgs->Info(constX);

}

void  threadLocalMemTest::printAlways(string toPrint) {
	RixContext* context = RixGetContext();
	RixMessages* msgs = (RixMessages*)context->GetRixInterface(k_RixMessages);
	const char * constX = toPrint.c_str();
	msgs->InfoAlways(constX);

}

int threadLocalMemTest::CreateInstanceData(RixContext& ctx, char const* handle, RixParameterList const* params, InstanceData* instance) {

	return 0;
}

void threadLocalMemTest::Finalize(RixContext& ctx) {
	if (generalMutex)
	{
		delete generalMutex;
		generalMutex = 0;
	}
}

RIX_SAMPLEFILTERCREATE{
	return new threadLocalMemTest();
}

RIX_SAMPLEFILTERDESTROY{
	delete((threadLocalMemTest*)filter);
}

/*
RixContext* context = RixGetContext();
RixMessages* msgs = (RixMessages*)context->GetRixInterface(k_RixMessages);
msgs->Info("x: ");
std::string X = to_string(direction.x);
const char * constX = X.c_str();
msgs->Info(constX);
*/
