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
#include <iomanip> // Include header to use std::setprecision and std::fixed
#include "libxl.h"

using namespace libxl;

// Define a struct for the fault injection result of Ascon.
// It returns both the minimum number of fault rounds and the average nibble faults needed to recover all 64 S-box inputs in one experiment.
struct Result {
    int returnFaultRound;
    double returnFaultNibble;
};

// int S_1[16] = { 4,31,26,9,27,8,29,6,30,7,0,17,16,1,22,15 };   // Ascon & ISAP - 1st bit stuck-at-0 fault, intersection = 1
// int S_1[16] = { 11,20,21,2,5,18,3,28,19,14,13,24,12,25,10,23 }; // Ascon & ISAP - 1st bit stuck-at-1 fault
// int S_1[16] = { 4,11,31,20,26,21,9,2,27,5,8,18,29,3,6,28 };   // Ascon & ISAP - 5th bit stuck-at-0 fault, intersection = 1
int S_1[16] = { 30,19,7,14,0,13,17,24,16,12,1,25,22,10,15,23 };   // Ascon & ISAP - 5th bit stuck-at-1 fault

// int S_1[16] = { 8,30,6,16,22,3,17,4,11,29,1,23,28,9,31,10 };  // SYCON - 1st bit stuck-at-0 fault, intersection = 1
// int S_1[16] = { 19,7,25,13,15,24,12,27,0,20,14,26,21,2,18,5 }; // SYCON - 1st bit stuck-at-1 fault
// int S_1[16] = { 8,19,6,25,22,15,17,12,11,0,1,14,28,21,31,18 }; // SYCON - 2nd bit stuck-at-0 fault, intersection = 1
// int S_1[16] = { 30,7,16,13,3,24,4,27,29,20,23,26,9,2,10,5 };   // SYCON - 2nd bit stuck-at-1 fault
// int S_1[16] = { 8,19,30,7,6,25,16,13,11,0,29,20,1,14,23,26 };  // SYCON - 4th bit stuck-at-0 fault, intersection = 1
// int S_1[16] = { 22,15,3,24,17,12,4,27,28,21,9,2,31,18,10,5 };  // SYCON - 4th bit stuck-at-1 fault
// int S_1[16] = { 8,19,30,7,6,25,16,13,22,15,3,24,17,12,4,27 };  // SYCON - 5th bit stuck-at-0 fault, intersection = 1
// int S_1[16] = { 11,0,29,20,1,14,23,26,28,21,9,2,31,18,10,5 };  // SYCON - 5th bit stuck-at-1 fault

int Sbox[64] = { 0 };     // Store correct input values of the last-round S-box in Ascon Finalization
int f_Sbox[64] = { 0 };   // Store faulty S-box inputs after fault (3rd bit removed)
int fault[64] = { 0 };    // Store injected nibble faults

void set_Sbox(int Sbox[]) {
    // Initialize random number generator
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> distribution(0, 31);

    // Fill the Sbox array with random values from 0 to 31
    for (int i = 0; i < 64; ++i) {
        int randomNum = distribution(gen);
        Sbox[i] = randomNum;
    }
}

void set_fault(int F[]) {
    // Initialize random number generator
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> distribution(0, 15);

    // Fill the fault array with random values from 0 to 15
    for (int i = 0; i < 64; ++i) {
        int randomNum = distribution(gen);
        F[i] = randomNum;
    }
}

void delete_1st_bit(int S[]) {  // Used for ISAP and SYCON where the stuck-at fault occurs at LSB

    int temp_s[5];
    int result;
    for (int i = 0; i < 64; ++i) {
        temp_s[0] = Sbox[i] % 2;
        temp_s[1] = (Sbox[i] / 2) % 2;
        temp_s[2] = (Sbox[i] / 4) % 2;
        temp_s[3] = (Sbox[i] / 8) % 2;
        temp_s[4] = (Sbox[i] / 16) % 2;

        // Uncomment the appropriate line below to simulate different stuck-at fault bit positions
        // result = temp_s[4] * 8 + temp_s[3] * 4 + temp_s[2] * 2 + temp_s[1]; // 1st bit stuck-at (LSB)
        // result = temp_s[4] * 8 + temp_s[3] * 4 + temp_s[2] * 2 + temp_s[0]; // 2nd bit stuck-at
        // result = temp_s[4] * 8 + temp_s[2] * 4 + temp_s[1] * 2 + temp_s[0]; // 4th bit stuck-at
        result = temp_s[3] * 8 + temp_s[2] * 4 + temp_s[1] * 2 + temp_s[0];   // 5th bit stuck-at (MSB)

        // Save result back to Sbox
        Sbox[i] = result;
    }
}

// Calculate the intersection of two sets
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
    // Define a 3D dynamic array: 16¡Á4¡Á?
    std::vector<std::vector<std::vector<int>>> differ_LSB_2(16, std::vector<std::vector<int>>(4));

    // Solve differential mappings
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 32; j++) {
            for (int in = 0; in < 16; in++) {
                out = S_1[in] ^ S_1[i ^ in];
                if (j == out) {
                    //differ_LSB_2[i][j % 4].push_back(in);     // ASCON uses LSB x4 and x3 as tag
                    differ_LSB_2[i][(j >> 3) % 4].push_back(in);  // ISAP uses MSB x0 and x1 as tag
                    //differ_LSB_2[i][(j >> 1) % 4].push_back(in); // Sycon uses mid bits x2 and x3 as tag
                }
            }
            //std::sort(differ_LSB_2[i][j % 4].begin(), differ_LSB_2[i][j % 4].end()); // ASCON
            std::sort(differ_LSB_2[i][(j >> 3) % 4].begin(), differ_LSB_2[i][(j >> 3) % 4].end()); // ISAP
            //std::sort(differ_LSB_2[i][(j >> 1) % 4].begin(), differ_LSB_2[i][(j >> 1) % 4].end()); // Sycon
        }
    }

    //for (int i = 0; i < 16; ++i) {
    //    for (int j = 0; j < 4; ++j) {
    //        // Print current cell elements
    //        if (differ_LSB_2[i][j].size() > 0) {
    //            std::cout << "  LSB_2[" << i << "][" << j << "]: ";
    //            for (int k = 0; k < differ_LSB_2[i][j].size(); ++k) {
    //                std::cout << differ_LSB_2[i][j][k] << ",";
    //            }
    //        }
    //    }
    //    std::cout << std::endl;
    //}
    //std::cout << std::endl;
    //std::cout << std::endl;

    // Set random S-box values
    set_Sbox(Sbox);
    //for (int i = 0; i < 64; ++i) {
    //    Sbox[i] = 31 - i % 32;
    //}

    //// Print S-box values
    //std::cout << "Generated Sbox :\n";
    //for (int i = 0; i < 64; ++i) {
    //    std::cout << Sbox[i] << " ";
    //}
    //std::cout << std::endl;


        // Write 5-bit S-box values to column 2 of row Num in the Excel sheet
    std::wstring S;
    for (int i = 0; i < 64; ++i) {
        S += std::to_wstring(Sbox[i]) + L",";
    }
    sheet->writeStr(Num, 1, S.c_str());

    delete_1st_bit(Sbox);

    // Number of fault injections
    const int COUNT = 100;
    int count;
    // When temp reaches 64, all S-box values are uniquely identified ¡ª experiment ends
    int temp = 0;

    // Define a 3D vector: COUNT ¡Á 64 ¡Á ?, to store possible S-box values after each fault injection
    std::vector<std::vector<std::vector<int>>> Intersection(COUNT, std::vector<std::vector<int>>(64));
    // Define a 2D vector: 64 ¡Á ?, to store intersections of possible S-box input values
    std::vector<std::vector<int>> Intersec(64);

    std::wstring Count_nibble;
    // Define Countnibble to record the round index when each S-box input is uniquely identified
    int Countnibble[64] = { 0 };

    for (count = 0; count < COUNT; ++count) {

        // Generate random fault values
        set_fault(fault);
        //for (int i = 0; i < 64; ++i) {
        //    fault[i] = i % 16;
        //}

        // Write 4-bit fault values to columns 5, 8, 11, ... of row Num
        std::wstring f;
        //std::cout << "\nRound " << count + 1 << " nibble faults:\n";
        for (int i = 0; i < 64; ++i) {
            //std::cout << fault[i] << " ";
            f += std::to_wstring(fault[i]) + L",";
        }
        sheet->writeStr(Num, 5 + count * 3, f.c_str());
        //std::cout << std::endl;

        // Apply faults
        for (int i = 0; i < 64; ++i) {
            f_Sbox[i] = Sbox[i] ^ fault[i];
        }

        // Write S-box output differentials to columns 6, 9, 12, ... of row Num
        std::wstring dif;
        //std::cout << "Round " << count + 1 << " S-box output differentials:\n";
        for (int i = 0; i < 64; ++i) {
            //std::cout << (S_1[Sbox[i]] ^ S_1[f_Sbox[i]]) << " ";
            dif += std::to_wstring(S_1[Sbox[i]] ^ S_1[f_Sbox[i]]) + L",";
        }
        sheet->writeStr(Num, 6 + count * 3, dif.c_str());
        //std::cout << std::endl;
        //std::cout << std::endl;

        // Fill the intersection array ¡ª note: index tag should match the bit scheme (low/high/mid)
        for (int i = 0; i < 64; ++i) {
            //Intersection[count][i] = differ_LSB_2[fault[i]][(S_1[Sbox[i]] ^ S_1[f_Sbox[i]]) % 4]; // ASCON
            Intersection[count][i] = differ_LSB_2[fault[i]][((S_1[Sbox[i]] ^ S_1[f_Sbox[i]]) >> 3) % 4]; // ISAP
            //Intersection[count][i] = differ_LSB_2[fault[i]][((S_1[Sbox[i]] ^ S_1[f_Sbox[i]]) >> 1) % 4]; // Sycon
        }

        // Initialize S-box intersections on first round
        // Write all possible S-box input sets to column 7 of row Num
        std::wstring Sb;
        if (count == 0) {
            for (int i = 0; i < 64; ++i) {
                Intersec[i] = Intersection[0][i];
                Sb += L"{";
                for (int j = 0; j < Intersec[i].size(); ++j) {
                    Sb += std::to_wstring(Intersec[i][j]) + L" ";
                }
                Sb += L"},";
                sheet->writeStr(Num, 7, Sb.c_str());
                //std::cout << "S-box[" << i << "] possible inputs: ";
                //for (int j = 0; j < Intersec[i].size(); ++j) {
                //    std::cout << Intersec[i][j] << " ";
                //}
                //std::cout << std::endl;
            }
        }

        // On subsequent rounds, intersect new candidate values with previous ones
        // Write updated S-box candidate sets to columns 10, 13, 16, ... of row Num
        std::wstring Sbb;
        if (count > 0) {
            for (int i = 0; i < 64; ++i) {
                Intersec[i] = calculateIntersection(Intersection[count][i], Intersec[i]);
                //std::cout << "S-box[" << i << "] possible inputs: ";
                Sbb += L"{";
                for (int j = 0; j < Intersec[i].size(); ++j) {
                    //std::cout << Intersec[i][j] << " ";
                    Sbb += std::to_wstring(Intersec[i][j]) + L" ";
                }
                Sbb += L"},";
                sheet->writeStr(Num, 7 + count * 3, Sbb.c_str());
                temp += Intersec[i].size();
                //std::cout << std::endl;
                if ((Intersec[i].size() == 1) && (Countnibble[i] == 0)) { // Record round if value becomes unique
                    Countnibble[i] = count + 1;
                }
            }
            if (temp == 64) {
                //std::cout << "After round " << count + 1 << ", all S-box values are uniquely determined.\n";
                break;
            }
            else {
                temp = 0;
            }
        }
    }

    int sum = 0;
    for (int i = 0; i < 64; ++i) {
        //std::cout << Countnibble[i] << " ";
        Count_nibble += std::to_wstring(Countnibble[i]) + L",";
        sum += Countnibble[i];
    }

    double Average_Nibble = double(sum) / 64;
    Count_nibble += L"\nAverage nibble faults required for 64 S-boxes: " + std::to_wstring(Average_Nibble);

    // Output results of a single experiment to Excel

    // Write 4-bit S-box values to column 3 of row Num
    std::wstring Sf;
    for (int i = 0; i < 64; ++i) {
        Sf += std::to_wstring(Sbox[i]) + L",";
    }
    sheet->writeStr(Num, 2, Sf.c_str());

    // Write minimum rounds needed to uniquely determine all S-box inputs to column 4
    std::wstring newNumberStr = std::to_wstring(count + 1);
    sheet->writeStr(Num, 3, newNumberStr.c_str());

    // Write nibble fault count required to recover each S-box input to column 5
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

    const int trial_Num = 100;   // Number of DFA trials per batch
    int Count[trial_Num] = { 0 };
    double Countnibble[trial_Num] = { 0 };
    int temp = 0;
    double temp1 = 0;

    // Create an Excel document object
    libxl::Book* book = xlCreateBook();
    book->setKey(L"libxl", L"windows-28232b0208c4ee0369ba6e68abv6v5i3");
    if (book) {

        libxl::Sheet* sheet = book->addSheet(L"Sheet1"); // Add a worksheet

        // Set column titles
        sheet->writeStr(0, 1, L"5-bit S-box values");
        sheet->writeStr(0, 2, L"4-bit S-box values");
        sheet->writeStr(0, 3, L"Minimum rounds to determine all S-box inputs");
        sheet->writeStr(0, 4, L"Nibble faults required per S-box input");

        for (int i = 0; i < 100; ++i) { // Maximum 100 fault injections per trial
            std::wstring i_str = std::to_wstring(i + 1);

            std::wstring output_str_1 = L"Trial " + i_str + L" 4-bit fault values";
            std::wstring output_str_2 = L"Trial " + i_str + L" S-box output differentials";
            std::wstring output_str_3 = L"Trial " + i_str + L" possible S-box input sets";

            sheet->writeStr(0, 5 + 3 * i, output_str_1.c_str());
            sheet->writeStr(0, 6 + 3 * i, output_str_2.c_str());
            sheet->writeStr(0, 7 + 3 * i, output_str_3.c_str());
        }

        // Set row labels and run each trial
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

        // End program timer
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        // Compute averages and write to Excel header
        double Average = double(temp) / double(trial_Num);
        double Averagenibble = double(temp1) / double(trial_Num);
        std::wstring newStr = L"Average fault injection rounds: " + std::to_wstring(Average)
            + L", average nibble injections: " + std::to_wstring(Averagenibble)
            + L"; total experiment time: " + std::to_wstring(duration.count() / 1'000'000.0) + L" s.";
        std::cout << Average << std::endl;
        std::cout << Averagenibble << std::endl;
        sheet->writeStr(0, 0, newStr.c_str());

        // Save Excel file
        book->save(L"Ascon_ISAP_Sycon_trials.xlsx");

        // Release Excel object
        book->release();
        std::cout << "Excel file generated successfully!" << std::endl;
    }
    else {
        std::cerr << "Unable to create Excel document object." << std::endl;
    }

    system("pause");
    return 0;
}


