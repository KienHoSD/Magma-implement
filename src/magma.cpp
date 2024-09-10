#include "magma.hpp"
#include "helper.hpp"

using std::array;
using std::cerr;
using std::cin;
using std::cout;
using std::string;
using std::invalid_argument;
using std::out_of_range;
using std::stoul;
using std::vector;
using std::reverse;

const vector<vector<uint8_t>> s_box = {
  {0xC, 0x4, 0x6, 0x2, 0xA, 0x5, 0xB, 0x9, 0xE, 0x8, 0xD, 0x7, 0x0, 0x3, 0xF, 0x1},
  {0x6, 0x8, 0x2, 0x3, 0x9, 0xA, 0x5, 0xC, 0x1, 0xE, 0x4, 0x7, 0xB, 0xD, 0x0, 0xF},
  {0xB, 0x3, 0x5, 0x8, 0x2, 0xF, 0xA, 0xD, 0xE, 0x1, 0x7, 0x4, 0xC, 0x9, 0x6, 0x0},
  {0xC, 0x8, 0x2, 0x1, 0xD, 0x4, 0xF, 0x6, 0x7, 0x0, 0xA, 0x5, 0x3, 0xE, 0x9, 0xB},
  {0x7, 0xF, 0x5, 0xA, 0x8, 0x1, 0x6, 0xD, 0x0, 0x9, 0x3, 0xE, 0xB, 0x4, 0x2, 0xC},
  {0x5, 0xD, 0xF, 0x6, 0x9, 0x2, 0xC, 0xA, 0xB, 0x7, 0x8, 0x1, 0x4, 0x3, 0xE, 0x0},
  {0x8, 0xE, 0x2, 0x5, 0x6, 0x9, 0x1, 0xC, 0xF, 0x4, 0xB, 0x0, 0xD, 0xA, 0x3, 0x7},
  {0x1, 0x7, 0xE, 0xD, 0x0, 0x5, 0x8, 0x3, 0x4, 0xF, 0xA, 0x6, 0x9, 0xC, 0xB, 0x2}
};


uint32_t Magma::f(const uint32_t &Ri, const uint32_t &Ki){
  uint32_t res1=0, res2=0, res3=0;
  res1 = Ri + Ki; // already module 2^32 due to data type uint32_t
  for(int i=0;i<NUM_OF_HALFBLOCKNIBBLE;i++){
    res2 += s_box[i][res1 >> (NUM_OF_BITSPERHALFBLOCK - NUM_OF_BITSPERNIBBLE*(i+1)) & 0xf] << NUM_OF_BITSPERNIBBLE*(NUM_OF_HALFBLOCKNIBBLE-i-1);
  }
  res3 = rot11(res2);
  return res3;
}

void Magma::round_function(uint32_t &L0, uint32_t &R0, const bool &is_encrypt){
  uint32_t Li, Ri, Lip1=L0, Rip1=R0;
  int round_to_swap = is_encrypt ? 24 : 8;
  for(int round = 0; round < NUM_OF_ROUNDS; round++){
    Li = Lip1;
    Ri = Rip1;
    if(round == round_to_swap){
      reverse(key_block.begin(),key_block.end());
    }
    if(is_encrypt){
      Lip1 = Ri;
      Rip1 = Li ^ f(Ri,key_block[round%NUM_OF_KEYBLOCK]);
    }
    else{
      Lip1 = Ri ^ f(Li,key_block[round%NUM_OF_KEYBLOCK]);
      Rip1 = Li;
    }
  }
  reverse(key_block.begin(),key_block.end());
  L0 = Lip1; R0 = Rip1;
}

void Magma::inner_encrypt_decrypt(const vector<uint8_t> &input, vector<uint8_t> &output, const bool &is_encrypt){
  if (input.size() % NUM_OF_BLOCKBYTE != 0){
    throw std::runtime_error("Input length must be a multiple of 8");
    return;
  }

  // matching output to input size
  output.resize(input.size());
  
  uint32_t L,R;
  for(size_t current_block_index = 0; current_block_index < input.size(); current_block_index += NUM_OF_BLOCKBYTE){
    block_to_LR(input, current_block_index, L, R);
    round_function(L, R, is_encrypt);
    LR_to_block(output, current_block_index, L, R);
  }
}

void Magma::encrypt(const vector<uint8_t> &plaintext, vector<uint8_t> &ciphertext){
  inner_encrypt_decrypt(plaintext,ciphertext,true);
}

void Magma::decrypt(const vector<uint8_t> &ciphertext, vector<uint8_t> &plaintext){
  inner_encrypt_decrypt(ciphertext,plaintext,false);
}

void Magma::inner_encrypt_decrypt_parallel(const std::vector<uint8_t> &input, std::vector<uint8_t> &output, const unsigned int &numThreads, const bool &is_encrypt){
  if (input.size() % NUM_OF_BLOCKBYTE != 0){
    throw std::runtime_error("Input length must be a multiple of 8");
    return;
  }

  output.resize(input.size());
  unsigned int chunkSize = (input.size() / numThreads) / NUM_OF_BLOCKBYTE * NUM_OF_BLOCKBYTE;
  std::vector<std::future<void>> futures;

  for (unsigned int i = 0; i < numThreads; ++i)
  {
    unsigned int start = i * chunkSize;
    unsigned int end = (i == numThreads - 1) ? input.size() : start + chunkSize;

    futures.push_back(std::async(std::launch::async, [this, &input, &output, start, end, is_encrypt]()  // Capture is_encrypt in the lambda's capture list
    {
      Magma threadLocalMagma(this->key_array);  // Each thread creates its own instance of Magma
      std::vector<uint8_t> chunkOutput(end - start);
      threadLocalMagma.inner_encrypt_decrypt(
        std::vector<uint8_t>(input.begin() + start, input.begin() + end),
        chunkOutput, 
        is_encrypt  // Encrypt mode
      );
      std::copy(chunkOutput.begin(), chunkOutput.end(), output.begin() + start); 
    }));
  }

  for (auto &future : futures)
  {
    future.get();
  }
}

void Magma::encryptParallel(const vector<uint8_t> &plaintext, vector<uint8_t> &ciphertext, unsigned int numThreads){
  inner_encrypt_decrypt_parallel(plaintext,ciphertext,numThreads,true);
}

void Magma::decryptParallel(const vector<uint8_t> &ciphertext, vector<uint8_t> &plaintext, unsigned int numThreads){
  inner_encrypt_decrypt_parallel(ciphertext,plaintext,numThreads,false);
}

void Magma::set_sbox(const vector<vector<uint8_t>> &s_box){
  if(s_box.size() != NUM_OF_HALFBLOCKNIBBLE){
    throw invalid_argument("Invalid s_box size");
    return;
  }
  for(int i=0;i<NUM_OF_HALFBLOCKNIBBLE;i++){
    if(s_box[i].size() != 16){
      throw invalid_argument("Invalid s_box[i] size");
      return;
    }
  }
  this->s_box = s_box;
}

void Magma::key_schedule()
{
  for(int i=0;i<NUM_OF_KEYBLOCK;i++){
    key_block[i] = 0;
    for(int j=0;j<NUM_OF_KEYBLOCKBYTE;j++){
      key_block[i] <<= NUM_OF_BITSPERBYTE;
      key_block[i] += key_array[i*NUM_OF_KEYBLOCKBYTE+j];
    }
  }
}