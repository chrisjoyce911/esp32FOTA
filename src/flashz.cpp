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

#include "flashz.hpp"
#include "esp_task_wdt.h"

#ifdef ARDUINO
#include "esp32-hal-log.h"
#else
#include "esp_log.h"
#endif

// ESP32 log tag
static const char *TAG __attribute__((unused)) = "FLASHZ";

#define INFLATOR_STREAM_BUFF_SIZE   128
#define INFLATOR_STREAM_DELAY_MS    5
#define INFLATOR_STREAM_DELAY_CTR   50


// Inflator class implementation
bool Inflator::init(){
    rdy = false;

    if (!dictBuff)
        dictBuff = (uint8_t *)malloc(sizeof(uint8_t) * TINFL_LZ_DICT_SIZE);

    if (!dictBuff)
        return false;   // OOM

    if (!m_decomp)
        m_decomp = new tinfl_decompressor;

    if (!m_decomp){
        delete dictBuff;
        dictBuff = nullptr;
        return false;   // OOM
    }

    reset();
    rdy = true;
    return rdy;
}

void Inflator::reset(){
    if (m_decomp)
        tinfl_init(m_decomp);

    dict_free = TINFL_LZ_DICT_SIZE;
    dict_begin = dict_offset = 0;

    avail_in = total_in = total_out = 0;

    decomp_status = TINFL_STATUS_NEEDS_MORE_INPUT;
    decomp_flags = TINFL_FLAG_PARSE_ZLIB_HEADER;          // compressed stream MUST have a proper zlib header
}

void Inflator::end(){
    rdy = false;
    delete m_decomp;
    m_decomp = nullptr;
    delete dictBuff;
    dictBuff = nullptr;
}

int Inflator::inflate(bool final){
    if (!next_in)
        return MZ_STREAM_ERROR;

    if (!dict_free)
        return MZ_NEED_DICT;

    if (final)
        decomp_flags &= ~TINFL_FLAG_HAS_MORE_INPUT;
    else
        decomp_flags |= TINFL_FLAG_HAS_MORE_INPUT;

    size_t in_bytes = avail_in, out_bytes = dict_free;

    // decompress as may input as available or as long as free dict space is available
    decomp_status = tinfl_decompress(m_decomp, next_in, &in_bytes, dictBuff, dictBuff + dict_offset, &out_bytes, decomp_flags);

    next_in += in_bytes;    // advance the input buffer pointer to the number of consumed bytes
    avail_in -= in_bytes;   // decrement input buffer counter
    total_in += in_bytes;   // increment total input cntr
    total_out += out_bytes; // increment total output cntr

    dict_offset = (dict_offset + out_bytes) & (TINFL_LZ_DICT_SIZE - 1);
    dict_free -= out_bytes;

    if (decomp_status < 0)
        return MZ_DATA_ERROR; /* Stream is corrupted (there could be some uncompressed data left in the output dictionary - oh well). */

    if ((decomp_status == TINFL_STATUS_NEEDS_MORE_INPUT) && final )    /* if deflator need more input and we demand it's a final call, than something must be wrong with a stream */
        return MZ_STREAM_ERROR;

    if (decomp_status == TINFL_STATUS_HAS_MORE_OUTPUT)    /* if deflator can't fit more data to the output buf, than need to flush a buff */
        return MZ_NEED_DICT;

    //return MZ_OK;       // this should be the only
    return ((decomp_status == TINFL_STATUS_DONE) && (final)) ? MZ_STREAM_END : MZ_OK;
};

int Inflator::inflate_block_to_cb(const uint8_t* inBuff, size_t len, inflate_cb_t callback, bool final, size_t chunk_size){
    if (!rdy)
        return MZ_BUF_ERROR;    // inflator not initialized

    next_in = inBuff;
    avail_in = len;

    decomp_flags &= ~( TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF );  // use internal ring buffer for decompression

    for (;;){
        unsigned int _to = total_out;
        int err = inflate(final);                                   // inflate as much in-data as possible

        if (err < 0){
            ESP_LOGW(TAG, "inflate failure - MZ_ERR: %d, inflate status: %d", err, decomp_status);
            return err;                                             // exit on any error
        }

        size_t deco_data_len = (dict_offset - dict_begin) & (TINFL_LZ_DICT_SIZE - 1);
        //ESP_LOGD(TAG, "+inflate chunk - mz_err:%d, ddl:%d, dfree:%u, tin:%u, tout:%u", err, deco_data_len, dict_free, total_in, total_out);

        if (!dict_offset && !dict_begin && total_out > _to)
            deco_data_len = TINFL_LZ_DICT_SIZE;     // jackpot - a full dict worth of data

        /**
         * call the callback if:
         * - no free space in dict
         * - accumulated data in dict is >= prefered chunk size
         * - it's a final input chunk
         */
        if (!dict_free || deco_data_len >= chunk_size || final){
            //ESP_LOGD(TAG, "dict stat: dfree:%u, ddl:%u, end:%u, tin:%d, tout:%d", dict_free, deco_data_len, final, total_in, total_out);

            /**
             * iterate the callback while:
             * - no space in dict
             * - have data in dict and it's a final chunk
             * - have data in dict >= prefered chunk size
             * 
             */
            while (!dict_free || (final && (bool)deco_data_len) || (deco_data_len >= chunk_size)){
                ESP_LOGD(TAG, "CB - idx:%u, head:%u, dbgn:%u, dend:%u, ddatalen:%u, avin:%u, tin:%u, tout:%u, fin:%d", total_out, dictBuff, dict_begin, dict_offset, deco_data_len, avail_in, total_in, total_out, final);  //  && (err == MZ_STREAM_END)

                // callback can consume only a portion of data from dict
                size_t consumed = callback(total_out - deco_data_len, dictBuff + dict_begin, deco_data_len, final && err == MZ_STREAM_END);

                if (!consumed || consumed > deco_data_len)      // it's an error not to consume or consume too much of dict data
                    return MZ_ERRNO;

                // clear the dict if all the data has been consumed so far
                if (consumed == deco_data_len){
                    dict_free = TINFL_LZ_DICT_SIZE;
                    dict_offset = 0;
                    dict_begin = 0;
                } else {
                    dict_begin = (dict_begin+consumed) & (TINFL_LZ_DICT_SIZE - 1);     // offset deco data pointer in dict
                }

                deco_data_len -= consumed;
            }
        }

        // if we are done with this chunk of input, than quit
        if (!avail_in || err == MZ_STREAM_END)
            return err;

        esp_task_wdt_reset();           // feed the dog, flashing highly compressed data (like almost empty FS image) could trigger WDT

        // go another inflate round
    }
}


int Inflator::inflate_stream_to_cb(Stream &data, int size, inflate_cb_t callback, size_t chunk_size){
    uint8_t buff[INFLATOR_STREAM_BUFF_SIZE];    // stream buffer

    int ctr = INFLATOR_STREAM_DELAY_CTR;        // stream wait/retry counter
    do {
        size_t available = data.available();
        if (!available && size){
            delay(INFLATOR_STREAM_DELAY_MS);
            if (--ctr)
                continue;
            else
                return MZ_STREAM_ERROR;
        }
        ctr = INFLATOR_STREAM_DELAY_CTR;

        // fill the buff from a stream
        int len = data.readBytes(buff, (available > sizeof(buff)) ? sizeof(buff) : available);

        // inflate buff
        int err = inflate_block_to_cb(buff, len, callback, (len == size), chunk_size);
        if (err < 0){
            //ESP_LOGI(TAG, "compressed buff: %02X%02X%02X%02X%02X%02X", buff[0], buff[1], buff[2], buff[3], buff[4], buff[5]);
            return err;
        }

        size -= len;
    } while(size > 0);

    //ESP_LOGW(TAG, "inflate stream %d bytes left", size);
    return size ? MZ_STREAM_ERROR : MZ_STREAM_END;
}

void Inflator::getstat(deco_stat_t &stat){
    stat.in_bytes = total_in;
    stat.out_bytes = total_out;
}



/**    FlashZ Class implementation    **/

bool FlashZ::beginz(size_t size, int command, int ledPin, uint8_t ledOn, const char *label){
    if (!deco.init())       // allocate Inflator memory
        return false;

    mode_z = true;
    return begin(size, command, ledPin, ledOn, label);
}

size_t FlashZ::writez(const uint8_t *data, size_t len, bool final){
    if (!mode_z)
        return write((uint8_t*)data, len);   // this cast to (uint8_t*) is a very dirty hack, but Arduino's Updater lib is missing constness on data pointer

    int err = deco.inflate_block_to_cb(data, len, [this](size_t i, const uint8_t* d, size_t s, bool f) -> int { return flash_cb(i, d, s, f); }, final);

    if (err >= MZ_OK)                       // intermediate or last chunk, ok
        return len;

    ESP_LOGE(TAG, "Inflate ERROR: %d", err);

    return 0;                               // deco error, assume that no data has been written, signal to the caller that something is wrong
}

void FlashZ::abortz(){
    abort();
    deco.end();
    mode_z = false;
}

bool FlashZ::endz(bool evenIfRemaining){
    deco.end();
    mode_z = false;
    return end(evenIfRemaining);
}

int FlashZ::flash_cb(size_t index, const uint8_t* data, size_t size, bool final){
    if (!size)
        return 0;

    size_t len;
    if (final){
        len = size;
    } else {
        // try to align writes to flash sector size
        len = size <= SPI_FLASH_SEC_SIZE ? size : size - (size % SPI_FLASH_SEC_SIZE);
    }
    size_t _w = write((uint8_t*)data, len);     // this cast to (uint8_t*) is a very dirty hack, but Arduino's Updater lib is missing constness on data pointer
    if (_w != len){
        //ESP_LOGI(TAG, "magic: %02X%02X%02X%02X%02X%02X", data[0], data[1], data[2], data[3], data[4], data[5]);
        ESP_LOGE(TAG, "ERROR, flashed %d of %d bytes chunk, err: %s!", _w, len, errorString());
        return 0;                               // if written size is less than requested, consider it as a fatal error, since I can't determine proccessed delated size
    }

    ESP_LOGI(TAG, "flashed %u bytes", _w);

    return _w;
}

size_t FlashZ::writezStream(Stream &data, size_t len){
    if (!mode_z)
        return writeStream(data);

    int err = deco.inflate_stream_to_cb(data, len, [this](size_t i, const uint8_t* d, size_t s, bool f) -> int { return flash_cb(i, d, s, f); });

    ESP_LOGI(TAG, "inflate stream err status: %d", err);

    deco_stat_t s;
    deco.getstat(s);
    return s.in_bytes;
}
