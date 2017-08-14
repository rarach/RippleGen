///////////////////////////////////////////////////////////////////////////////////
//
// ripplegen.cpp 
//
// Copyright (c) 2013 Eric Lombrozo
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// Portions of the source were taken from the ripple and bitcoin reference
// implementations. Their licensing terms and conditions must be respected.
// Please see accompanying LICENSE file for details.
//
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "RippleAddress.h"
#include <iostream>
#include <stdint.h>
#include <boost/thread.hpp>
#include <openssl/rand.h>

#define UPDATE_ITERATIONS 1000


//RARACH: COMPARE THIS TO THE OTHER VERSION. I START TO FEEL LIKE THAT ONE IS ACTUALLY BETTER :-O


using namespace std;

boost::mutex mutex;
bool fDone = false;			//TODO: what is this good for?

uint64_t start_time;
uint64_t total_searched;

const char* ALPHABET = "rpshnaf39wBUDNEGHJKLM4PQRST7VWXYZ2bcdeCg65jkm8oFqi1tuvAxyz";

char charHex(int iDigit)			//TODO: move this to utils.h
{
    return iDigit < 10 ? '0' + iDigit : 'A' - 10 + iDigit;
}

void getRand(unsigned char *buf, int num)
{
    if (RAND_bytes(buf, num) != 1)
    {
        assert(false);
        throw std::runtime_error("Entropy pool not seeded");
    }
}

void LoopThread(unsigned int n, uint64_t eta50, string* ppattern,
                string* pmaster_seed, string* pmaster_seed_hex, string* paccount_id)
{
    RippleAddress naSeed;
    RippleAddress naAccount;
    string        pattern = *ppattern;
    string        account_id;

    uint128 key;
    getRand(key.begin(), key.size());

    uint64_t count = 0;
    uint64_t last_count = 0;
    do {
        naSeed.setSeed(key);
        RippleAddress naGenerator = createGeneratorPublic(naSeed);
        naAccount.setAccountPublic(naGenerator.getAccountPublic(), 0);
        account_id = naAccount.humanAccountID();
        count++;
        if (count % UPDATE_ITERATIONS == 0) {
            boost::unique_lock<boost::mutex> lock(mutex);				//TODO: No way. Get rid of the mutex
            total_searched += count - last_count;
            last_count = count;
            uint64_t nSecs = time(NULL) - start_time;
            double speed = (1.0 * total_searched)/nSecs;
            const char* unit = "seconds";
            double eta50f = eta50/speed;
            if (eta50f > 100) {
                unit = "minutes";
                eta50f /= 60;

                if (eta50f > 100) {
                    unit = "hours";
                    eta50f /= 60;

                    if (eta50f > 48) {
                        unit = "days";
                        eta50f /= 24;
                    }
                }
            }

			//TODO: simplify this. We don't need that much info
            cout << "# Thread " << n << ": " << count << " seeds." << endl
                 << "#" << endl
                 << "#           Total Speed:    " << speed << " seeds/second" << endl
                 << "#           Total Searched: " << total_searched << endl
                 << "#           Total Time:     " << nSecs << " seconds" << endl
                 << "#           ETA 50%:        " << eta50f << " " << unit << endl
                 << "#           Last:           " << account_id << endl
                 << "#           Pattern:        " << pattern << endl
                 << "#" << endl;
        }
        key++;
        boost::this_thread::yield();
    } while ((account_id.substr(0, pattern.size()) != pattern) && !fDone);

    boost::unique_lock<boost::mutex> lock(mutex);
    if (fDone) return;
    fDone = true;

    cout << "#    *** Found by thread " << n << ". ***" << endl
         << "#" << endl;

    *pmaster_seed = naSeed.humanSeed();
    *pmaster_seed_hex = naSeed.getSeed().ToString();
    *paccount_id = account_id;
}


// isPatternValid can be changed depending on encoding being used.
bool isPatternValid(const string& pattern, string& msg)
{
    // Check for valid ripple account id.
    // TODO: Implement it correctly
    if (pattern.size() == 0) {
        msg = "Pattern cannot be empty.";
        return false;
    }
    if (pattern[0] != 'r') {
        msg = "Pattern must begin with an 'r'.";
        return false;
    }
    return true;
}

// eta50 = ceiling(1/log_2(n/(n-1))) where n = 58^length
//   the minimum number of iterations such that there's at
//   least a 50% chance of finding a match.
uint64_t getEta50(const string& pattern)
{
    const uint64_t eta50[] = { 0, 0, 40, 2332, 135241, 7843997, 454951843, 26387206905ull,
                               1530458000460ull, 8876654026661ull, 5148460713546319ull,
                               298610721385686486ull, 17319421840369816160ull };
    unsigned int len = pattern.size();
    if (len > 12) return 0xffffffffffffffffull;
    return eta50[len]; 
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        cout << "# Usage: " << argv[0] << " [pattern] [threads=cpus available]" << endl
             << "#" << endl;
        return 0;
    }

    string pattern = argv[1];
    string msg;
    if (!isPatternValid(pattern, msg)) {
        cout << "# " << msg << endl
             << "#" << endl;
        return -2;
    }

    unsigned int cpus = boost::thread::hardware_concurrency();
    unsigned int threads = (argc >= 3) ? strtoul(argv[2], NULL, 0) : cpus;
    if (threads == 0) {
        cout << "# You must run at least one thread." << endl
             << "#" << endl;
        return -1;
    }

    cout << "# CPUs detected: " << cpus << endl
         << "#" << endl
         << "# Running " << threads << " thread" << (threads == 1 ? "" : "s") << "." << endl
         << "#" << endl
         << "# Generating seed for pattern \"" << pattern << "\"..." << endl
         << "#" << endl;

    uint64_t eta50 = getEta50(pattern);

    start_time = time(NULL);
    string master_seed, master_seed_hex, account_id;
    vector<boost::thread*> vpThreads;
    for (unsigned int i = 0; i < threads; i++)
        vpThreads.push_back(new boost::thread(LoopThread, i, eta50, &pattern, &master_seed, &master_seed_hex, &account_id));

    for (unsigned int i = 0; i < threads; i++)
        vpThreads[i]->join();
   
    for (unsigned int i = 0; i < threads; i++)
        delete vpThreads[i];
 
    cout << "#    master seed:     " << master_seed << endl
         << "#    master seed hex: " << master_seed_hex << endl
         << "#    account id:      " << account_id << endl
         << "#" << endl;

    return 0;
}
