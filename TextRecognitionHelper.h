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
		std::vector<cv::Ptr<cv::text::OCRTesseract>> getOCRs();
		void setOCRs(std::vector<cv::Ptr<cv::text::OCRTesseract>> o);
		std::vector<cv::Ptr<cv::text::ERFilter>> getERFilters1();
		std::vector<cv::Ptr<cv::text::ERFilter>> getERFilters2();
		void setERFilters1(std::vector <cv::Ptr<cv::text::ERFilter>> er1);
		void setERFilters2(std::vector <cv::Ptr<cv::text::ERFilter>> er2);
		//static TextRecognitionHelper *instance();
	private:
		//static TextRecognitionHelper *s_instance;
		int num_ocrs;
		int RECOGNITION;
		std::vector<cv::Rect>* nm_boxes;
		std::vector<cv::Ptr<cv::text::OCRTesseract>> ocrs;
		std::vector <cv::Ptr<cv::text::ERFilter>> erfilter1;
		std::vector <cv::Ptr<cv::text::ERFilter>> erfilter2;
	};
#endif // !TEXTRECOGNITIONHELPER_HEADER

