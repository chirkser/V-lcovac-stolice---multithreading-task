#ifndef __PROGTEST__
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cfloat>
#include <cassert>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <string>
#include <vector>
#include <array>
#include <iterator>
#include <set>
#include <list>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <stack>
#include <deque>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <condition_variable>
#include <pthread.h>
#include <semaphore.h>
#include "progtest_solver.h"
#include "sample_tester.h"
using namespace std;
#endif /* __PROGTEST__ */

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

std::mutex cout_mu;

class WorkThreadManager
{
    std::mutex mu;
    uint32_t freeThreads;
    std::condition_variable freeThreadsCondVar;
    void threadFunction(std::function<void()>& func) {
        func();
        std::unique_lock<std::mutex> lock(mu);
        freeThreads++;
        lock.unlock();
        freeThreadsCondVar.notify_one();
    }

public:
    WorkThreadManager(uint32_t threadCount) : freeThreads(threadCount) {}
    // request a thread and start execution
    void execute(std::function<void()> thrFunc) {
        std::unique_lock<std::mutex> lock(mu);
        while (freeThreads <= 0) {
            freeThreadsCondVar.wait(lock);
        }
        freeThreads--;
        lock.unlock();

        std::thread thread(std::bind(&WorkThreadManager::threadFunction, this, thrFunc));
        thread.detach();
    }
};

void calc(ASheet sheet) {
    int** thicknessData = new int*[sheet->m_Length];
    for (int i = 0; i < sheet->m_Length; i++) {
        thicknessData[i] = new int[sheet->m_Width];
        for (int j = 0; j < sheet->m_Width; j++) {
            thicknessData[i][j] = sheet->m_Thickness[sheet->m_Width * i + j];
        }
    }
    for (auto& relDevProblem : sheet->m_RelDev) {
        relDevProblem.second = maxRectByRelDev(thicknessData, sheet->m_Width, sheet->m_Length, relDevProblem.first);
    }
    for (auto& volumeProblem : sheet->m_Volume) {
        volumeProblem.second = maxRectByVolume(thicknessData, sheet->m_Width, sheet->m_Length, volumeProblem.first);
    }
    for (auto& minMaxProblem : sheet->m_MinMax) {
        minMaxProblem.second = maxRectByMinMax(thicknessData, sheet->m_Width, sheet->m_Length, minMaxProblem.first.m_Lo,
                                               minMaxProblem.first.m_Hi);
    }
    for (int i = 0; i < sheet->m_Length; i++) {
        delete[] thicknessData[i];
    }
    delete[] thicknessData;
}

class ProductionLineManager
{
    AProductionLine line;
    std::thread readThread, doneThread;
    std::shared_ptr<WorkThreadManager> thrManager;
    std::mutex mu;
    std::function<void()> informFinished;
    std::condition_variable taskDoneCondVar;
    uint32_t retTask = 0;
    uint32_t currTask = 0;
    std::map<uint32_t, ASheet> completedTasks;
    std::atomic_bool gotAllTasks{false};

public:
    ProductionLineManager(AProductionLine line, std::function<void()> informFinished)
      : line(line), informFinished(informFinished) {}
    void start(std::shared_ptr<WorkThreadManager> manager) {
        thrManager = manager;
        readThread = std::thread(std::bind(&ProductionLineManager::taskGetter, this));
        doneThread = std::thread(std::bind(&ProductionLineManager::taskReturner, this));
    }
    ~ProductionLineManager() {
        readThread.join();
        doneThread.join();
    }

private:
    void taskGetter() {
        while (true) {
            auto sheet = line->getSheet();
            if (!sheet) {
                gotAllTasks = true;
                return;
            }
            thrManager->execute(std::bind(&ProductionLineManager::calcFunc, this, currTask++, sheet));
        }
    }
    void taskReturner() {
        std::unique_lock<std::mutex> lock(mu);
        while (!(gotAllTasks && retTask == currTask)) {
            // Wait till next task in line becomes available
            while (completedTasks.find(retTask) == completedTasks.end()) {
                taskDoneCondVar.wait(lock);
            }
            line->doneSheet(completedTasks.at(retTask));
            completedTasks.erase(retTask++);
        }
        lock.unlock();
        informFinished();
    }
    void calcFunc(uint32_t id, ASheet sheet) {
        calc(sheet);
        std::lock_guard<std::mutex> guard(mu);
        completedTasks[id] = sheet;
        taskDoneCondVar.notify_one();
    }
};

class CQualityControl
{
    std::vector<std::unique_ptr<ProductionLineManager>> lines;
    uint32_t activeLineCount;
    std::mutex mu;
    std::condition_variable lineFinishedCondVar;

    void lineFinishedCallback() {
        std::lock_guard<std::mutex> guard(mu);
        if (--activeLineCount == 0) {
            lineFinishedCondVar.notify_all();
        };
    };

public:
    static void checkAlgorithm(ASheet sheet) { calc(sheet); }
    void addLine(AProductionLine line) {
        lines.push_back(
          std::make_unique<ProductionLineManager>(line, std::bind(&CQualityControl::lineFinishedCallback, this)));
    }
    void start(int workThreads) {
        auto threadManager = std::make_shared<WorkThreadManager>(workThreads);
        activeLineCount = lines.size();
        for (auto& line : lines) {
            line->start(threadManager);
        }
    }
    void stop() {
        std::unique_lock<std::mutex> lock(mu);
        while (activeLineCount != 0) {
            lineFinishedCondVar.wait(lock);
        }
    }
};
//-------------------------------------------------------------------------------------------------------------------------------------------------------------
#ifndef __PROGTEST__
int main(void) {
    CQualityControl control;
    AProductionLineTest line = std::make_shared<CProductionLineTest>();
    // AProductionLineTest line2 = std::make_shared<CProductionLineTest>();
    control.addLine(line);
    // control.addLine(line2);

    control.start(4);
    control.stop();
    if (!line->allProcessed()) throw std::logic_error("(some) sheets were not correctly processsed");
    // if (!line2->allProcessed()) throw std::logic_error("(some) sheets were not correctly processsed");
    return 0;
}
#endif /* __PROGTEST__ */
