#pragma once
#ifndef TEXTRECOGNITIONHELPER_HEADER
#define TEXTRECOGNITIONHELPER_HEADER

#include "opencv2/core/cvstd.hpp"
#include "opencv2/core/core.hpp"
#include "opencv2/text/erfilter.hpp"
#include "opencv2/text.hpp"
#include "opencv2/core/utility.hpp"
#include "opencv2/core/cvdef.h"
#include <cstddef>
#include <cstring>
#include <cctype>
#include <vector>


	class TextRecognitionHelper sealed
	{
	public:
		std::vector<cv::Ptr<cv::text::ERFilter>>* getERFilters1();
		std::vector<cv::Ptr<cv::text::ERFilter>>* getERFilters2();
		//vector<ERFilter*> getERFilters1();
		//vector<ERFilter*> getERFilters2();
		void setERFilters1(std::vector <cv::Ptr<cv::text::ERFilter>>* er1);
		void setERFilters2(std::vector <cv::Ptr<cv::text::ERFilter>>* er2);
		//void setERFilters1(vector <ERFilter*>);
		//void setERFilters2(vector <ERFilter*>);
		//static TextRecognitionHelper *instance();
	private:
		//static TextRecognitionHelper *s_instance;
		int num_ocrs;
		int RECOGNITION;
		std::vector<cv::Rect>* nm_boxes;
		//std::vector<cv::Ptr<cv::text::OCRTesseract>>* ocrs;
		//vector< Ptr<OCRHMMDecoder> > decoders;
		std::vector <cv::Ptr<cv::text::ERFilter>>* erfilter1;
		std::vector <cv::Ptr<cv::text::ERFilter>>* erfilter2;
	};
#endif // !TEXTRECOGNITIONHELPER_HEADER

