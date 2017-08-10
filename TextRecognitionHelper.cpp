#include "pch.h"
#include "TextRecognitionHelper.h"

#include "opencv2/text.hpp"
#include "opencv2/core/utility.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/features2d.hpp"

using namespace concurrency;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Holographic;
using namespace Windows::UI::Core;
using namespace std;
using namespace std::placeholders;
using namespace cv;
using namespace cv::text;

/*TextRecognitionHelper::TextRecognitionHelper()
{
	
}*/

	/*static TextRecognitionHelper TextRecognitionHelper::*instance()
	{
		if (!s_instance)
			s_instance = new TextRecognitionHelper;
		return s_instance;
	}*/
	vector <Ptr<ERFilter>> TextRecognitionHelper::getERFilters1()
	{
		return erfilter1;
	}
	vector <Ptr<ERFilter>> TextRecognitionHelper::getERFilters2()
	{
		return erfilter2;
	}
	void TextRecognitionHelper::setERFilters1(vector <Ptr<ERFilter>> er1)
	{
		erfilter1 = er1;
	}
	void TextRecognitionHelper::setERFilters2(vector <Ptr<ERFilter>> er2)
	{
		erfilter2 = er2;
	}

