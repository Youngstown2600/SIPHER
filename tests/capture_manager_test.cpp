#include "trunkmonkey/CaptureManager.h"
#include <iostream>
#include <stdexcept>
#include <string>
int main(){try{const auto interfaces=trunkmonkey::CaptureManager::availableInterfaces();const auto hint=trunkmonkey::CaptureManager::permissionHint();if(hint.empty())throw std::runtime_error("capture permission hint is empty");std::cout<<"capture tool="<<trunkmonkey::CaptureManager::captureTool()<<" interfaces="<<interfaces.size()<<"\n";return 0;}catch(const std::exception&e){std::cerr<<"capture manager test failed: "<<e.what()<<'\n';return 1;}}
