#pragma once
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <algorithm>

namespace OIV
{
    typedef void(*PixelConvertFunc)(uint8_t* i_dest, const uint8_t* i_src, size_t start, size_t end);
    class PixelUtil
    {
    public:
        struct alignas(1) BitTexel8 { uint8_t X; };

        struct alignas(1) BitTexel16 : public BitTexel8 { uint8_t Y; };

        struct alignas(1) BitTexel24 : public BitTexel16 { uint8_t Z; };

        struct alignas(1) BitTexel32 : public BitTexel24 { uint8_t W; };


        // A function to copy a same format texel from one place to another
        template <class BIT_TEXEL_TYPE>
        static __forceinline  void CopyTexel(void* dest, const std::size_t idxDest, const void* src, const std::size_t idxSrc)
        {
            reinterpret_cast<BIT_TEXEL_TYPE*>(dest)[idxDest] = reinterpret_cast<const  BIT_TEXEL_TYPE*>(src)[idxSrc];

        }

        static void Convert(PixelConvertFunc convertFunc, uint8_t** i_dest, const uint8_t* i_src, const uint8_t dstTexelSizeinBits, const size_t numTexels)
        {
            using namespace std;
            //TODO: fine tune the minimum size required to open helper threads
            const size_t MegaBytesPerThread = 6;
            //TODO: choose max threads 
            const uint8_t maxThreads = 5;


            const size_t bytesPerThread = MegaBytesPerThread * 1024 * 1024;
            const size_t bytesPerPixel = dstTexelSizeinBits / 8;
            const size_t texelsPerThread = bytesPerThread / bytesPerPixel;
            static std::thread threads[maxThreads];
            const uint8_t totalThreads = std::min(maxThreads, static_cast<uint8_t>(numTexels / texelsPerThread));
            *i_dest = new uint8_t[numTexels * bytesPerPixel];
            uint8_t* dest = *i_dest;
            if (totalThreads > 0)
            {
                const size_t segmentSize = numTexels / (totalThreads + 1);

                for (uint8_t threadNum = 0; threadNum < totalThreads; threadNum++)
                {
                    threads[threadNum] = std::thread
                    (
                        [convertFunc, threadNum, i_src, dest, segmentSize]()
                    {

                        const size_t start = threadNum * segmentSize;
                        const size_t end = start + segmentSize;
                        convertFunc(dest, i_src, start, end);
                    }
                    );
                }

                const size_t start = (totalThreads)* segmentSize;
                const size_t end = start + segmentSize;
                convertFunc(dest, i_src, start, end);

                for (uint8_t i = 0; i < totalThreads; i++)
                    threads[i].join();
            }
            else
            {
                // single threaded implementation.
                convertFunc(dest, i_src, 0, numTexels);
            }

        }

        static void BGR24ToRGBA32(uint8_t* i_dest, const uint8_t* i_src, std::size_t start, std::size_t end)
        {
            uint32_t* dst = (uint32_t*)i_dest;
            BitTexel24 * src = (BitTexel24*)i_src;

            for (size_t i = start; i < end; i++)
            {
                dst[i] = 0xFF << 24 | (src[i].Z << 16) | (src[i].Y << 8) | (src[i].X << 0);
            }
        }


        static void RGB24ToRGBA32(uint8_t* i_dest, const uint8_t* i_src, std::size_t start, std::size_t end)
        {
            uint32_t* dst = (uint32_t*)i_dest;
            BitTexel24 * src = (BitTexel24*)i_src;

            for (size_t i = start; i < end; i++)
            {
                dst[i] = 0xFF << 24 | (src[i].Z << 16) | (src[i].Y << 8) | (src[i].X << 0);
            }
        }

        static void BGRA32ToRGBA32(uint8_t* i_dest, const uint8_t* i_src, std::size_t start, std::size_t end)
        {
            BitTexel32* dst = (BitTexel32*)i_dest;
            BitTexel32 * src = (BitTexel32*)i_src;

            for (size_t i = start; i < end; i++)
            {
                dst[i].X = src[i].Z;
                dst[i].Y = src[i].Y;
                dst[i].Z = src[i].X;
                dst[i].W = src[i].W;
            }
        }

        //static void BGR32ToRGBA32(uint8_t** i_dest, uint8_t* i_src, size_t size)
        //{
        //    if (size % 3 != 0)
        //        throw std::logic_error("Data is not aligned");

        //    size_t totalTexels = size / 3;
        //    *i_dest = new uint8_t[totalTexels * 4];
        //    StopWatch stopWatch;
        //    
        //    //Simd solution, try to find away to set the alpha channel to 255 instead of 0
        //  /*  __m128i *src = (__m128i *)i_src;
        //    __m128i *dst = (__m128i *)*i_dest;
        //    __m128i mask = _mm_setr_epi8(0, 1, 2, -1, 3, 4, 5, -1, 6, 7, 8, -1, 9, 10, 11, -1);

        //    for (int i = 0; i<totalTexels; i += 16) {
        //        __m128i sa = _mm_load_si128(src);
        //        __m128i sb = _mm_load_si128(src + 1);
        //        __m128i sc = _mm_load_si128(src + 2);
        //        
        //        __m128i val = _mm_shuffle_epi8(sa, mask);
        //        _mm_store_si128(dst, val);
        //        val = _mm_shuffle_epi8(_mm_alignr_epi8(sb, sa, 12), mask);
        //        _mm_store_si128(dst + 1, val);
        //        val = _mm_shuffle_epi8(_mm_alignr_epi8(sc, sb, 8), mask);
        //        _mm_store_si128(dst + 2, val);
        //        val = _mm_shuffle_epi8(_mm_alignr_epi8(sc, sc, 4), mask);
        //        _mm_store_si128(dst + 3, val);

        //        src += 3;
        //        dst += 4;
        //    }
    };
}

