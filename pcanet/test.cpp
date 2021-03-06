#include <opencv2\opencv.hpp>
#include "PCANet.h"

#include <iostream>
#include <fstream>
#include <string>
using std::cout;
using std::endl;


int main(int argc, char** argv)
{
    int DIR_NUM = 40;
    int train_num=5;
    int test_num=5;

    PCANet pcaNet;
    pcaNet.load("pcanet.xml");
    cv::String sets = pcaNet.settings();
    cout << "PCANet test " << sets << endl;

    cv::String dir = "e:/media/faces/att_faces/";
    cv::Mat features,labels;
	for (int i=1; i<=DIR_NUM; i++)
    {
		for (int j=1; j<=train_num; j++)
        {
            cv::String path = cv::format("%s%c%d%s%d%s", dir.c_str(), 's', i, "\\", j, ".pgm");
            cv::Mat img = cv::imread(path, cv::IMREAD_GRAYSCALE);
            //cv::resize(img,img,cv::Size(110,110));
            img.convertTo(img, CV_32F, 1.0/255);
            features.push_back(pcaNet.extract(img));
            labels.push_back(i);
            cout << " " << i << " " << path << "\r";
		}
	}
    int64 e1 = cv::getTickCount();
    cout << "\n ====== Training SVM Classifier ======= \n" << endl;

    cv::Ptr<cv::ml::SVM> svm = cv::ml::SVM::create();
    svm->setC(1);
    svm->setKernel(cv::ml::SVM::LINEAR);
    svm->setTermCriteria(cv::TermCriteria(cv::TermCriteria::MAX_ITER+cv::TermCriteria::EPS, 1000, 1e-6));
    svm->train(features, cv::ml::ROW_SAMPLE, labels);
    int64 e2 = cv::getTickCount();
    double time = (e2 - e1) / cv::getTickFrequency();
    cout << " PCANet Training time: " << time << endl;

    cout << "\n ====== PCANet Testing ======= \n" << endl;
    float all = float(DIR_NUM * test_num);
    float correct = 0;
	int testNum = 5;
	for (int i=1; i<=DIR_NUM; i++)
    {
		for (int j = train_num + 1; j <= train_num + test_num; j++)
        {
            cv::String path = cv::format("%s%c%d%s%d%s", dir.c_str(), 's', i, "\\", j, ".pgm");
            cv::Mat img = cv::imread(path, cv::IMREAD_GRAYSCALE);
            //cv::resize(img,img,cv::Size(110,110));
            img.convertTo(img, CV_32F, 1.0/255);
            cv::Mat hashing = pcaNet.extract(img);
		    int predicted = (int)svm->predict(hashing);
		    if (predicted == i)
            {
			    correct++;
		    } 
            else
            {
                cout << " missed label: " << i << ", predicted: " << predicted << endl;
            }
		}
	}

    int64 e3 = cv::getTickCount();
    cout << " PCANet Test time: " << ((e3 - e2) / cv::getTickFrequency()) << endl;
    cout << " PCANet Test accuracy: " << (correct/all) << " " << (all-correct) << " / " << all << endl;
    return 0;
}
