#include <opencv2\opencv.hpp>
#include "PCANet.h"

#include <iostream>
#include <fstream>
#include <string>
using std::cout;
using std::endl;


int main(int argc, char** argv)
{
    cv::String dir = "funneled";
    //cv::String dir = "lfw3d_9000";
    std::vector<cv::String> files;
    glob(dir,files,true);
    if (files.empty())
    {
        cerr << " - no input!" << endl;
        return 1;
    }
    std::vector<cv::Mat> InImgs;
    for (size_t i=0; i<777; ++i)
    {
        size_t z = cv::theRNG().uniform(0,files.size());
        cv::Mat im = cv::imread(files[z],0);
        im.convertTo(im,CV_32F);
        InImgs.push_back(im);
    }


    PCANet pcaNet(9);
    pcaNet.addStage(8,32);
    pcaNet.addStage(2,12);
    cv::String sets = pcaNet.settings();
    cout << "PCANet train " << sets << endl;
    int64 e1 = cv::getTickCount();

    pcaNet.trainPCA(InImgs, false);

    int64 e2 = cv::getTickCount();
    double time = (e2 - e1) / cv::getTickFrequency();
    cout << " PCANet Training time: " << time << endl;

    pcaNet.save("pcanet2.xml");
    return 0;
}
