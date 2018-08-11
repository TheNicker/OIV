#pragma warning(disable : 4275 ) // disables warning 4275, pop and push from exceptions
#pragma warning(disable : 4251 ) // disables warning 4251, the annoying warning which isn't needed here...
#include "oiv.h"
#include "External\easyexif\exif.h"
#include "Interfaces\IRenderer.h"
#include "NullRenderer.h"
#include <ImageLoader.h>
#include <ImageUtil.h>
#include "Configuration.h"
#include "API/functions.h"
#include "FreeType/FreeTypeConnector.h"
#include "FreeType/FreeTypeHelper.h"
#include "Interfaces/IRendererDefs.h"


#if OIV_BUILD_RENDERER_D3D11 == 1
#include "../OIVD3D11Renderer/Include/OIVD3D11RendererFactory.h"
#endif


#if OIV_BUILD_RENDERER_GL == 1
#include "../OIVGLRenderer/OIVGLRendererFactory.h"
#endif

namespace OIV
{
    LLUtils::PointI32 OIV::GetClientSize() const
    {
        return fClientSize;
    }
  
    void OIV::RefreshRenderer()
    {
        UpdateGpuParams();
        fRenderer->Redraw();
    }

    IMCodec::ImageSharedPtr OIV::GetDisplayImage() const
    {
        return  GetImage(ImageHandleDisplayed);
    }

    void OIV::UpdateGpuParams()
    {
        fViewParams.showGrid = fShowGrid;
        fViewParams.uViewportSize = GetClientSize();
        fRenderer->SetViewParams(fViewParams);
    }

    bool OIV::IsImageDisplayed() const
    {
        return GetDisplayImage() != nullptr;
    }

    OIV_AxisAlignedRTransform OIV::ResolveExifRotation(unsigned short exifRotation) const
    {
        OIV_AxisAlignedRTransform rotation;
            switch (exifRotation)
            {
            case 3:
                rotation = AAT_Rotate180;
                break;
            case 6:
                rotation = AAT_Rotate90CW;
                break;
            case 8:
                rotation = AAT_Rotate90CCW;
                break;
            default:
                rotation = AAT_None;
            }
            return rotation;
    }

    IRendererSharedPtr OIV::CreateBestRenderer()
    {

#ifdef _MSC_VER 
     // Prefer Direct3D11 for windows.
    #if OIV_BUILD_RENDERER_D3D11 == 1
            return D3D11RendererFactory::Create();
    // Prefer Direct3D11 for windows.
    #elif  OIV_BUILD_RENDERER_GL == 1
        return GLRendererFactory::Create();
    #elif OIV_ALLOW_NULL_RENDERER == 1
        return IRendererSharedPtr(new NullRenderer());
    #else
        #error No valid renderers detected.
    #endif

#else // _MSC_VER
// If no windows choose GL renderer
    #if OIV_BUILD_RENDERER_GL == 1
        return GLRendererFactory::Create();
    #elif OIV_ALLOW_NULL_RENDERER == 1
        return IRendererSharedPtr(new NullRenderer());
    #else
        #error No valid renderers detected.
    #endif
#endif

        LL_EXCEPTION(LLUtils::Exception::ErrorCode::BadParameters, "Bad build configuration");

    }


#pragma region IPictureViewer implementation
    // IPictureViewr implementation
    ResultCode OIV::LoadFile(void* buffer, std::size_t size, char* extension, OIV_CMD_LoadFile_Flags flags, ImageHandle& handle)
    {
        if (buffer == nullptr || size == 0)
            return RC_InvalidParameters;

        using namespace IMCodec;
        ImageSharedPtr image = ImageSharedPtr(fImageLoader.Load(static_cast<uint8_t*>(buffer), size, extension, (flags & OIV_CMD_LoadFile_Flags::OnlyRegisteredExtension) != 0));

        if (image != nullptr)
        {
            if (flags & OIV_CMD_LoadFile_Flags::Load_Exif_Data)
            {
                easyexif::EXIFInfo exifInfo;
                if (exifInfo.parseFrom(static_cast<const unsigned char*>(buffer), static_cast<unsigned int>(size)) == PARSE_EXIF_SUCCESS)
                {
                    const_cast<ImageDescriptor::MetaData&>(image->GetDescriptor().fMetaData).exifOrientation = exifInfo.Orientation;
                }
            }
                
            handle = fImageManager.AddImage(image);

            return RC_Success;
        }
        else
        {
            return RC_FileNotSupported;
        }
    }

    ResultCode OIV::LoadRaw(const OIV_CMD_LoadRaw_Request& loadRawRequest, int16_t& handle) 
    {
        using namespace IMCodec;

        ImageDescriptor props;
        props.fProperties.Height = loadRawRequest.height;
        props.fProperties.Width = loadRawRequest.width;
        props.fProperties.NumSubImages = 0;
        props.fProperties.TexelFormatStorage = static_cast<IMCodec::TexelFormat>(loadRawRequest.texelFormat);
        props.fProperties.TexelFormatDecompressed = static_cast<IMCodec::TexelFormat>(loadRawRequest.texelFormat);
        props.fProperties.RowPitchInBytes = loadRawRequest.rowPitch;

        props.fData.AllocateAndWrite(loadRawRequest.buffer, props.fProperties.RowPitchInBytes * props.fProperties.Height);


        ImageSharedPtr image = ImageSharedPtr(new Image(props));
        image = IMUtil::ImageUtil::Transform(
            static_cast<IMUtil::AxisAlignedRTransform>(loadRawRequest.transformation), image);

        handle = fImageManager.AddImage(image);
        return RC_Success;

        
    }

    IMCodec::ImageSharedPtr OIV::ApplyExifRotation(IMCodec::ImageSharedPtr image) const
    {
        return IMUtil::ImageUtil::Transform(
            static_cast<IMUtil::AxisAlignedRTransform>(ResolveExifRotation(image->GetDescriptor().fMetaData.exifOrientation))
            , image);
    }

    ResultCode OIV::DisplayFile(const OIV_CMD_DisplayImage_Request& display_request)
    {
        ResultCode result = RC_Success;

        if (display_request.handle == ImageHandleNull)
        {
            fImageManager.RemoveImage(ImageHandleDisplayed);
            return result;
        }

        IMCodec::ImageSharedPtr image = fImageManager.GetImage(display_request.handle);
        if (image != nullptr)
        {
            OIV_CMD_DisplayImage_Flags display_flags = display_request.displayFlags;

            const bool applyExif = (display_flags & OIV_CMD_DisplayImage_Flags::DF_ApplyExifTransformation) != 0;
            if (applyExif)
                image = ApplyExifRotation(image);
            

            // Texel format supported by the renderer is currently RGBA.
            // support for other texel formats may save conversion.
            const IMCodec::TexelFormat targetTexelFormat = IMCodec::TexelFormat::I_R8_G8_B8_A8;

            switch (image->GetImageType())
            {
            case IMCodec::TexelFormat::F_X16:
                image = IMUtil::ImageUtil::Normalize<half_float::half>(image, targetTexelFormat);
                break;

            case IMCodec::TexelFormat::F_X24:
                LL_EXCEPTION(LLUtils::Exception::ErrorCode::NotImplemented, "CodecTiff, 24 bit float is not implemented");
                //image = IMUtil::ImageUtil::Normalize<half_float::half>(image, targetTexelFormat);
                break;

            case IMCodec::TexelFormat::F_X32:
                image = IMUtil::ImageUtil::Normalize<float>(image, targetTexelFormat,static_cast<IMUtil::ImageUtil::NormalizeMode>(display_request.normalizeMode));
                break;
            case IMCodec::TexelFormat::I_X8:
                image = IMUtil::ImageUtil::Normalize<int8_t>(image, targetTexelFormat, static_cast<IMUtil::ImageUtil::NormalizeMode>(display_request.normalizeMode));
                break;

            default:
                image = IMUtil::ImageUtil::Convert(image, targetTexelFormat);
            }


            if (image != nullptr)
            {
                if (fRenderer->SetImageBuffer(0,image) == RC_Success)
                {
                    fImageManager.ReplaceImage(ImageHandleDisplayed, image);

                    const bool resetScrollState = (display_flags & OIV_CMD_DisplayImage_Flags::DF_ResetScrollState) != 0;
                    const bool refreshRenderer = (display_flags & OIV_CMD_DisplayImage_Flags::DF_RefreshRenderer) != 0;


                    if (refreshRenderer)
                        RefreshRenderer();
                }
                else
                {
                    result = RC_RenderError;
                }
            }
            else
            {
                result = RC_PixelFormatConversionFailed;
            }
        }
        else
        {
            result = RC_InvalidImageHandle;
        }
     
        return result;
    }

    ResultCode OIV::CreateText(const OIV_CMD_CreateText_Request &request, OIV_CMD_CreateText_Response &response)
    {
    #if OIV_BUILD_FREETYPE == 1

        OIVString text = request.text;
        OIVString fontPath = request.fontPath;

        //std::string u8Text = LLUtils::StringUtility::ToUTF8<OIVCHAR>(text);
        //std::string u8FontPath = LLUtils::StringUtility::ToUTF8<OIVCHAR>(fontPath);

        IMCodec::ImageSharedPtr imageText = FreeTypeHelper::CreateRGBAText(
            LLUtils::StringUtility::ToAString(text),
            LLUtils::StringUtility::ToAString(fontPath),
            request.fontSize,
            request.backgroundColor);

        if (imageText != nullptr)
        {
            ImageHandle handle = fImageManager.AddImage(imageText);
            response.imageHandle = handle;
            fRenderer->SetImageBuffer(response.imageHandle, imageText);
            return RC_Success;
        }
        else
        {
            return RC_InvalidParameters;
        }


#else
        return RC_NotImplemented;
#endif
    }

    ResultCode OIV::SetSelectionRect(const OIV_CMD_SetSelectionRect_Request& selectionRect)
    {
        fRenderer->SetSelectionRect({ { selectionRect.rect.x0 ,selectionRect.rect.y0 },{ selectionRect.rect.x1 ,selectionRect.rect.y1 } });
        return RC_Success;
    }

    ResultCode OIV::ConverFormat(const OIV_CMD_ConvertFormat_Request& req)
    {
        using namespace IMCodec;
        ResultCode result = RC_Success;
        if (req.handle > 0 )
        {
            ImageSharedPtr original = fImageManager.GetImage(req.handle);
            if (original != nullptr)
            {
                ImageSharedPtr converted = IMUtil::ImageUtil::Convert(original, static_cast<TexelFormat>(req.format));
                if (converted != nullptr)
                    fImageManager.ReplaceImage(req.handle, converted);
                else
                    result = ResultCode::RC_BadConversion;
            }
            else
                result = ResultCode::RC_ImageNotFound;
            
        }
        return result;
    }

    ResultCode OIV::GetPixels(const OIV_CMD_GetPixels_Request & req,  OIV_CMD_GetPixels_Response & res)
    {
        IMCodec::ImageSharedPtr image = fImageManager.GetImage(req.handle);

        if (image != nullptr)
        {
            res.width = image->GetWidth();
            res.height = image->GetHeight();
            res.rowPitch = image->GetRowPitchInBytes();
            res.texelFormat = static_cast<OIV_TexelFormat>( image->GetImageType());
            res.pixelBuffer = image->GetConstBuffer();
            return RC_Success;
        }

        return RC_InvalidHandle;
        
    }

    ResultCode OIV::CropImage(const OIV_CMD_CropImage_Request& request, OIV_CMD_CropImage_Response& response)
    {
        ResultCode result = RC_Success;
        IMCodec::ImageSharedPtr imageToCrop = GetImage(request.imageHandle);
        if (imageToCrop == nullptr)
        {
            result = RC_ImageNotFound;
        }
        else
        {
            LLUtils::RectI32 imageRect = { { 0,0 } ,{ static_cast<int32_t> (imageToCrop->GetWidth())
                , static_cast<int32_t> (imageToCrop->GetHeight()) } };

            LLUtils::RectI32 subImageRect = { { request.rect.x0,request.rect.y0 },{ request.rect.x1,request.rect.y1 } };
            LLUtils::RectI32 cuttedRect = subImageRect.Intersection(imageRect);
            IMCodec::ImageSharedPtr subImage =
                IMUtil::ImageUtil::GetSubImage(imageToCrop, cuttedRect);

            if (subImage != nullptr)
            {
                ImageHandle handle = fImageManager.AddImage(subImage);
                response.imageHandle = handle;
                result = RC_Success;
            }
            else
            {
                result = RC_UknownError;
            }
        }
        return result;
    }

    ResultCode OIV::SetColorExposure(const OIV_CMD_ColorExposure_Request& exposure)
    {
        fRenderer->SetExposure(exposure);
        return RC_Success;
    }

    ResultCode OIV::GetTexelInfo(const OIV_CMD_TexelInfo_Request& texel_request, OIV_CMD_TexelInfo_Response& texelresponse)
    {
        
        IMCodec::ImageSharedPtr  image = GetImage(texel_request.handle);
        if (image != nullptr)
        {
            if (texel_request.x >= 0
                && texel_request.x < image->GetWidth()
                && texel_request.y >= 0
                && texel_request.y < image->GetHeight())
            {
                texelresponse.type = (OIV_TexelFormat)image->GetImageType();
                OIV_Util_GetBPPFromTexelFormat(texelresponse.type, &texelresponse.size);
                memcpy(texelresponse.buffer, image->GetBufferAt(texel_request.x, texel_request.y), texelresponse.size);
                    
                return RC_Success;
            }
            else return RC_UknownError;
        }

        return RC_ImageNotFound;
        
    }

    ResultCode OIV::SetImageProperties(const OIV_CMD_ImageProperties_Request& imageProperties)
    {
        fRenderer->SetImageProperties(imageProperties);
        return RC_Success;
    }

    ResultCode OIV::GetKnownFileTypes(OIV_CMD_GetKnownFileTypes_Response& res)
    {
        std::wstring knownFileTypes = fImageLoader.GetKnownFileTypes();
        std::string knownFileTypesAnsi =  LLUtils::StringUtility::ToAString(knownFileTypes);
        res.bufferSize = knownFileTypesAnsi.size() + 1;
        if (res.knownFileTypes != nullptr)
            memcpy(res.knownFileTypes, knownFileTypesAnsi.data(), knownFileTypesAnsi.size() + 1);
        
        return RC_Success;
    }

    ResultCode OIV::RegisterCallbacks(const OIV_CMD_RegisterCallbacks_Request& callbacks)
    {
        fCallBacks = callbacks;
        return RC_Success;
    }

    ResultCode OIV::UnloadFile(const ImageHandle handle)
    {
        ResultCode result = RC_Success;
        fImageManager.RemoveImage(handle);
        fRenderer->RemoveImage(handle);

        return result;
    }

    int OIV::Init()
    {
        static_assert(OIV_TexelFormat::TF_COUNT == static_cast<OIV_TexelFormat>( IMCodec::TexelFormat::COUNT), "Wrong array size");

        LLUtils::Exception::OnException.Add([this](LLUtils::Exception::EventArgs args)
        {
            if (fCallBacks.OnException != nullptr)
            {
                OIV_Exception_Args localArgs = { };
                localArgs.errorCode = static_cast<int>(args.errorCode);
                localArgs.callstack = args.callstack.c_str();
                localArgs.description = args.description.c_str();
                localArgs.systemErrorMessage = args.systemErrorMessage.c_str();
                localArgs.functionName = args.functionName.c_str();
                fCallBacks.OnException(localArgs);
            }
        }
        );

        fRenderer = CreateBestRenderer();
        fRenderer->Init(fParent);
        return 0;
    }


    int OIV::SetParent(std::size_t handle)
    {
        fParent = handle;
        return 0;
    }
    int OIV::Refresh()
    {
        RefreshRenderer();
        return 0;
    }

    IMCodec::ImageSharedPtr OIV::GetImage(ImageHandle handle) const
    {
        return fImageManager.GetImage(handle);
    }

    ResultCode OIV::GetFileInformation(ImageHandle handle, OIV_CMD_QueryImageInfo_Response& info)
    {
        using namespace  IMCodec;
        ImageSharedPtr image = GetImage(handle);

        if (image != nullptr)
        {
            info.width = image->GetWidth();
            info.height = image->GetHeight();
            info.rowPitchInBytes = image->GetRowPitchInBytes();
            info.bitsPerPixel = image->GetBitsPerTexel();
            info.NumSubImages = image->GetNumSubImages();
            return RC_Success;
        }

        return RC_InvalidImageHandle;
    }

    int OIV::SetTexelGrid(double gridSize)
    {
        fShowGrid = gridSize > 0.0;
        return RC_Success;
    }

  
    int OIV::SetClientSize(uint16_t width, uint16_t height)
    {
        fClientSize = { width, height };
        return 0;
    }

    ResultCode OIV::AxisAlignTrasnform(const OIV_CMD_AxisAlignedTransform_Request& request)
    {
        
        if (request.handle == ImageHandleDisplayed && GetDisplayImage() != nullptr)
        {
            IMCodec::ImageSharedPtr& image = fImageManager.GetImage(ImageHandleDisplayed);
            
            image = IMUtil::ImageUtil::Transform(static_cast<IMUtil::AxisAlignedRTransform>(request.transform), image);
            if (image != nullptr && fRenderer->SetImageBuffer(0,image) == RC_Success)
            {
                fImageManager.ReplaceImage(ImageHandleDisplayed, image);
                return RC_Success;
            }
        }
        else
        {
            IMCodec::ImageSharedPtr image = fImageManager.GetImage(request.handle);
            if (image != nullptr)
            {
                image = IMUtil::ImageUtil::Transform(static_cast<IMUtil::AxisAlignedRTransform>(request.transform), image);
                fImageManager.ReplaceImage(request.handle, image);
            }
        }
        return RC_UknownError;
    }
#pragma endregion

}
