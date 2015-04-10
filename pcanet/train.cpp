#include <opencv2\opencv.hpp>
#include "PCANet.h"

#include <iostream>
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
        cout << " - no input!" << endl;
        return 1;
    }

    std::vector<cv::Mat> images;
    for (size_t i=0; i<777; ++i)
    {
        size_t z = cv::theRNG().uniform(0,files.size());
        cv::Mat im = cv::imread(files[z],0);
        im.convertTo(im,CV_32F);
        images.push_back(im);
    }

    PCANet pcaNet(9);
    pcaNet.addStage(4,28);
    pcaNet.addStage(4,23);

    cout << "PCANet train " << pcaNet.settings() << endl;
    int64 e1 = cv::getTickCount();

    pcaNet.trainPCA(images, false);

    int64 e2 = cv::getTickCount();
    double time = (e2 - e1) / cv::getTickFrequency();
    cout << " PCANet Training time: " << time << endl;

    pcaNet.save("pcanet.xml");
    return 0;
}
