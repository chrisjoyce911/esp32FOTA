/*
    ESP32-FlashZ library

    This code implements a library for ESP32-xx family chips and provides an
    ability to upload zlib compressed firmware images during OTA updates.

    It derives from Arduino's UpdaterClass and uses in-ROM miniz decompressor to inflate
    libz compressed data during firmware flashing process

    Copyright (C) Emil Muratov, 2022
    GitHub: https://github.com/vortigont/esp32-flashz

    Lib code based on esptool's implementation https://github.com/espressif/esptool/
    so it inherits it's GPL-2.0 license

 *  This program or library is free software; you can redistribute it
 *  and/or modify it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 *  Public License version 2 for more details.
 *
 *  You should have received a copy of the GNU General Public License version 2
 *  along with this library; if not, get one at
 *  https://opensource.org/licenses/GPL-2.0
 */


#pragma once
#include <rom/miniz.h>
#include <Update.h>
#include <functional>

#ifndef ESP_IMAGE_HEADER_MAGIC
#define ESP_IMAGE_HEADER_MAGIC  0xE9
#endif
#define GZ_HEADER               0x1F
#define ZLIB_HEADER             0x78

#define FLASH_CHUNK_SIZE 2*SPI_FLASH_SEC_SIZE        // SPI NOR erase sector size is 4096 bytes, so let's take 2 sectors

// same defines as in miniz.h, excluded in Arduino (todo: add some guards here)
/* Return status codes. MZ_PARAM_ERROR is non-standard. */
enum
{
    MZ_OK = 0,
    MZ_STREAM_END = 1,
    MZ_NEED_DICT = 2,
    MZ_ERRNO = -1,
    MZ_STREAM_ERROR = -2,
    MZ_DATA_ERROR = -3,
    MZ_MEM_ERROR = -4,
    MZ_BUF_ERROR = -5,
    MZ_VERSION_ERROR = -6,
    MZ_PARAM_ERROR = -10000
};

struct deco_stat_t {
    size_t in_bytes;
    size_t out_bytes;
};


// inflator callback type
typedef std::function<int (size_t index, const uint8_t* data, size_t size, bool final)> inflate_cb_t;



class Inflator {
    bool rdy = 0;                   /* ready flag, depends on success mem alloc */

    // stream control vars
    const uint8_t *next_in;         /* pointer to next byte to read */
    unsigned int avail_in;          /* number of bytes available at next_in */
    unsigned int total_in;          /* total number of input bytes consumed so far */
    unsigned int total_out;         /* total number of inflated output bytes */
    size_t dict_begin, dict_offset, dict_free;   /* output dictionary offset pointer and free space counter */

    // deflator struct
    tinfl_decompressor *m_decomp = nullptr;

    // dictionary buff
    int decomp_flags;
    tinfl_status decomp_status;
    uint8_t* dictBuff = nullptr;     // heap buffer for deflated dict data

    int inflate(bool final = false);


public:

    ~Inflator(){ end(); }

    /**
     * @brief Intialize inflator
     * allocate mem structs and buffers
     * 
     * @return true 
     * @return false 
     */
    bool init();

    /**
     * @brief reset inflator to initial state
     * Inflator must be initialized
     * 
     */
    void reset();

    /**
     * @brief end up inflator and dealloc all memory
     * 
     */
    void end();

    void getstat(deco_stat_t &stat);

    /**
     * @brief inflate input buffer into internal dict an call the callback function on inflated data
     * by default callback is called only when output dict is full (32k), so it might skip a call if input block
     * has not enough input data to inflate dict buffer. Param chunk_size sets _prefered_ buffer size for callback.
     * It's OK to consume any amount of bytes via callback except 0. If callback returns 0 than it means an error state
     * for callback and signal to abort the Inflator.
     * 
     * @param inBuff - pointer to block of compressed data
     * @param len - buffer length
     * @param callback - callback function
     * @param chunk_size - prefered chunk size for callback
     * @return int - MZ_* exit code
     */
    int inflate_block_to_cb(const uint8_t* inBuff, size_t len, inflate_cb_t callback, bool final = false, size_t chunk_size = TINFL_LZ_DICT_SIZE);


    int inflate_stream_to_cb(Stream &data, int size, inflate_cb_t callback, size_t chunk_size = TINFL_LZ_DICT_SIZE);
};


/**
 * @brief FlashZ class derives from Arduino's UpdateClass and provides additional methods
 * to transparently flash libz (zz) compressed images. ESP32 does not (yet) support native compressed images
 * flashing via eboot as in ESP8266. So this class just decompresses input data on the fly and flashes
 * inflated image inplace, same way as esptool does
 * 
 */
class FlashZ : public UpdateClass {

    FlashZ(){};     // hidden c-tor
    ~FlashZ(){};    // hidden d-tor

    //deco_stat_t stat;
    bool mode_z = false;        // need to keep mode state for async writez() calls
    Inflator deco;

    /**
     * @brief callback for inflator
     * writes inflated firmware chunk to flash
     * 
     */
    int flash_cb(size_t index, const uint8_t* data, size_t size, bool final);    //> inflate_cb_t

    public:
        // this is a singleton, no copy's
        FlashZ(const FlashZ&) = delete;
        FlashZ& operator=(const FlashZ &) = delete;
        FlashZ(FlashZ &&) = delete;
        FlashZ & operator=(FlashZ &&) = delete;

        static FlashZ& getInstance(){
            static FlashZ flashz;
            return flashz;
        }

        /**
         * @brief initilize Inflator structs and UpdaterClass
         * 
         * @return true on success
         * @return false on Inflator mem allocation error or flash free space error
         */
        bool beginz(size_t size=UPDATE_SIZE_UNKNOWN, int command = U_FLASH, int ledPin = -1, uint8_t ledOn = LOW, const char *label = NULL);

        /**
         * @brief Writes a buffer to the flash and increments the address
         * Returns the amount of processed compressed bytes. Decompressed written size is usually larger
         * returns zero in case of any decompression error 
         * 
         * @param data 
         * @param len 
         * @return processed bytes
         */
        size_t writez(const uint8_t *data, size_t len, bool final);

        /**
         * @brief Read zlib compressed data from stream, decompress and write it to flash
         * size of the stream must be known in order to signal zlib inflator last chunk
         * 
         * @param data Stream object, usually data from a tcp socket
         * @param len total length of compressed data to read from stream
         * @return size_t number of bytes processed from a stream
         */
        size_t writezStream(Stream &data, size_t len);

        /**
         * @brief abort running inflator and flash update process
         * also releases inflator memory
         */
        void abortz();
        
        /**
         * @brief release inflator memory and run UpdateClass.end()
         * returns status of end() call
         * 
         * @return true 
         * @return false 
         */
        bool endz(bool evenIfRemaining = true);

        /**
         * @brief request stat data from the inflator
         * return amount of input/inflated bytes processed so far
         * 
         * @param stat stat structure to update with data
         */
        void getstat(deco_stat_t &stat){ deco.getstat(stat); };
};