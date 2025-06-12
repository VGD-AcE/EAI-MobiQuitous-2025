#include <stdio.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <bitset>
#include <set>
#include <algorithm>
#include <random>
#include <iterator>
#include <numeric>
#include <chrono>
#include <iomanip> // Add header to use std::setprecision and std::fixed
#include "libxl.h"

using namespace libxl;

// Define the SHAMASH fault injection process result structure: includes fault round and nibble average
struct Result {
    int returnFaultRound;
    double returnFaultNibble;
};

// SHAMASH 5-bit S-box
int S[32] = { 16,14,13,2,11,17,21,30,7,24,18,28,26,1,12,6,
               31,25,0,23,20,22,8,27,4,3,19,5,9,10,29,15 };

// Correct S-box inputs in final round
int Sbox[64] = { 0 };
// Faulty S-box inputs in final round
int f_Sbox[64] = { 0 };
// Injected fault values
int fault[64] = { 0 };

void set_Sbox(int Sbox[]) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> distribution(0, 31);

    for (int i = 0; i < 64; ++i) {
        int randomNum = distribution(gen);
        Sbox[i] = randomNum;
    }
}

void set_fault(int F[]) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> distribution(0, 31);

    for (int i = 0; i < 64; ++i) {
        int randomNum = distribution(gen);
        F[i] = randomNum;
    }
}

// Compute intersection of two sets
std::vector<int> calculateIntersection(const std::vector<int>& set1, const std::vector<int>& set2) {
    std::vector<int> intersection;
    std::set_intersection(
        set1.begin(), set1.end(),
        set2.begin(), set2.end(),
        std::back_inserter(intersection)
    );
    return intersection;
}

Result Ascon_trial(Sheet* sheet, int Num)
{
    int out;

    // Define a 3D dynamic array: 32 ¡Á 4 ¡Á ?
    std::vector<std::vector<std::vector<int>>> differ_LSB_2(32, std::vector<std::vector<int>>(4));

    // Solve the differential equation
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            for (int in = 0; in < 32; in++) {
                out = S[in] ^ S[i ^ in];
                if (j == out) {
                    differ_LSB_2[i][(j >> 3) % 4].push_back(in);
                }
            }
            std::sort(differ_LSB_2[i][(j >> 3) % 4].begin(), differ_LSB_2[i][(j >> 3) % 4].end());  // Sort the set (important)
        }
    }

    set_Sbox(Sbox);

    std::wstring STRING;
    for (int i = 0; i < 64; ++i) {
        STRING += std::to_wstring(Sbox[i]) + L",";
    }
    sheet->writeStr(Num, 1, STRING.c_str());

    // Perform fault injection
    const int COUNT = 100;
    int count;
    // When temp == 64, it means all S-box input values are uniquely determined ¡ª end trial
    int temp = 0;

    // Define a 3D array: COUNT ¡Á 64 ¡Á ?, to store candidate inputs after each fault injection
    std::vector<std::vector<std::vector<int>>> Intersection(COUNT, std::vector<std::vector<int>>(64));

    // Define a 2D array: 64 ¡Á ?, to store intersections between rounds
    std::vector<std::vector<int>> Intersec(64);

    std::wstring Count_nibble;

    // Array to store the round number when each S-box input is uniquely determined
    int Countnibble[64] = { 0 };

    for (count = 0; count < COUNT; ++count) {
        // Generate random fault values
        set_fault(fault);

        // Inject faults into S-box inputs
        for (int i = 0; i < 64; ++i) {
            f_Sbox[i] = Sbox[i] ^ fault[i];
        }

        // Output 5-bit fault values and faulty S-box inputs
        std::wstring f;
        for (int i = 0; i < 64; ++i) {
            f += std::to_wstring(fault[i]) + L",";
        }
        f += L"*****Faulty S-box inputs:";
        for (int i = 0; i < 64; ++i) {
            f += std::to_wstring(f_Sbox[i]) + L",";
        }
        sheet->writeStr(Num, 5 + count * 3, f.c_str());

        // Output S-box output differences and correct outputs
        std::wstring dif;
        for (int i = 0; i < 64; ++i) {
            dif += std::to_wstring(S[Sbox[i]] ^ S[f_Sbox[i]]) + L",";
        }
        dif += L"*****S-box outputs:";
        for (int i = 0; i < 64; ++i) {
            dif += std::to_wstring(S[Sbox[i]]) + L",";
        }
        sheet->writeStr(Num, 6 + count * 3, dif.c_str());

        // Fill the intersection array with possible inputs
        for (int i = 0; i < 64; ++i) {
            Intersection[count][i] = differ_LSB_2[fault[i]][((S[Sbox[i]] ^ S[f_Sbox[i]]) >> 3) % 4];
        }

        // For the first injection, initialize intersection sets
        std::wstring Sb;
        if (count == 0) {
            for (int i = 0; i < 64; ++i) {
                Intersec[i] = Intersection[0][i];
                Sb += L"{";
                for (int j = 0; j < Intersec[i].size(); ++j) {
                    Sb += std::to_wstring(Intersec[i][j]) + L" ";
                }
                Sb += L"},";
            }
            sheet->writeStr(Num, 7, Sb.c_str());
        }

        // For subsequent injections, update intersections
        std::wstring Sbb;
        if (count > 0) {
            for (int i = 0; i < 64; ++i) {
                Intersec[i] = calculateIntersection(Intersection[count][i], Intersec[i]);
                Sbb += L"{";
                for (int j = 0; j < Intersec[i].size(); ++j) {
                    Sbb += std::to_wstring(Intersec[i][j]) + L" ";
                }
                Sbb += L"},";
                temp += Intersec[i].size();
                // Record the first round when this S-box input becomes unique
                if ((Intersec[i].size() == 1) && (Countnibble[i] == 0)) {
                    Countnibble[i] = count + 1;
                }
            }
            sheet->writeStr(Num, 7 + count * 3, Sbb.c_str());

            if (temp == 64) {
                // All S-box inputs uniquely determined
                break;
            }
            else {
                temp = 0;
            }
        }
    }

    int sum = 0;
    for (int i = 0; i < 64; ++i) {
        Count_nibble += std::to_wstring(Countnibble[i]) + L",";
        sum += Countnibble[i];
    }
    double Average_Nibble = double(sum) / 64;
    Count_nibble += L"\nAverage nibble faults for 64 S-boxes: " + std::to_wstring(Average_Nibble);

    // Output results to Excel
    std::wstring newNumberStr = std::to_wstring(count + 1);
    sheet->writeStr(Num, 3, newNumberStr.c_str());
    sheet->writeStr(Num, 4, Count_nibble.c_str());

    Result result;
    result.returnFaultRound = count + 1;
    result.returnFaultNibble = Average_Nibble;

    return result;
}


int main()
{
    // Start program timer
    auto start = std::chrono::high_resolution_clock::now();

    const int trial_Num = 3;  // Number of trials in one DFA experiment
    int Count[trial_Num] = { 0 };
    double Countnibble[trial_Num] = { 0 };
    int temp = 0;
    double temp1 = 0;

    // Create an Excel document object
    libxl::Book* book = xlCreateBook();
    book->setKey(L"libxl", L"windows-28232b0208c4ee0369ba6e68abv6v5i3");
    if (book) {

        libxl::Sheet* sheet = book->addSheet(L"Sheet1"); // Add a worksheet

        // sheet->setCol(1, 0, 30); // Set column widths
        // sheet->setCol(1, 1, 35);
        // sheet->setCol(1, 2, 35);
        // sheet->setCol(1, 3, 35);
        // sheet->setCol(1, 4, 35);

        // Set row headers
        sheet->writeStr(0, 1, L"5-bit S-box values");
        sheet->writeStr(0, 2, L"Faulty 5-bit S-box values");
        sheet->writeStr(0, 3, L"Min trials needed to uniquely determine S-box inputs");
        sheet->writeStr(0, 4, L"5-bit faults per S-box to recover inputs");

        for (int i = 0; i < 100; ++i) { // Set up to 100 fault injections per trial
            // Convert i to wide string
            std::wstring i_str = std::to_wstring(i + 1);

            // Build output strings
            std::wstring output_str_1 = L"Trial " + i_str + L" fault values";
            std::wstring output_str_2 = L"Trial " + i_str + L" S-box output differences";
            std::wstring output_str_3 = L"Trial " + i_str + L" possible S-box input sets";

            sheet->writeStr(0, 5 + 3 * i, output_str_1.c_str());
            sheet->writeStr(0, 6 + 3 * i, output_str_2.c_str());
            sheet->writeStr(0, 7 + 3 * i, output_str_3.c_str());
        }

        // Run trials and collect results
        for (int i = 0; i < trial_Num; ++i) {
            std::wstring i_str = std::to_wstring(i + 1);
            std::wstring output_str = L"Trial " + i_str + L":";
            sheet->writeStr(i + 1, 0, output_str.c_str());

            Result result = Ascon_trial(sheet, i + 1);
            Count[i] = result.returnFaultRound;
            Countnibble[i] = result.returnFaultNibble;
            temp += Count[i];
            temp1 += Countnibble[i];
        }

        // End timer
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        // Calculate average values over all trials and write to top-left cell
        double Average = double(temp) / double(trial_Num);
        double Averagenibble = double(temp1) / double(trial_Num);
        std::wstring newStr = L"Average fault injection rounds: " + std::to_wstring(Average) +
            L", Average 5-bit nibble faults: " + std::to_wstring(Averagenibble) +
            L", Total experiment time: " + std::to_wstring(duration.count() / 1'000'000.0) + L"s.";
        std::cout << Average << std::endl;
        std::cout << Averagenibble << std::endl;
        sheet->writeStr(0, 0, newStr.c_str());

        // Save Excel file
        book->save(L"SHAMASH_trials.xlsx");

        // Release resources
        book->release();
        std::cout << "Excel file generated successfully!" << std::endl;
    }
    else {
        std::cerr << "Failed to create Excel document object" << std::endl;
    }

    system("pause");
    return 0;
}
