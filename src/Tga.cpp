#include "Tga.h"

#include "os.h"
#include "os_graphics.h"
#include "string_util.h"
#include "PianoGameError.h"

Tga* Tga::Load(const std::wstring &resource_name)
{

#ifdef WIN32
   // This is for future use.  For now, we're limiting
   // ourselves to the current executable only.
   const HMODULE module = 0;

   HRSRC resource_id = FindResource(module, resource_name.c_str(), L"GRAPHICS");
   if (!resource_id) throw PianoGameError(L"Couldn't find TGA resource.");
   
   HGLOBAL resource = LoadResource(module, resource_id);
   if (!resource) throw PianoGameError(L"Couldn't load TGA resource.");

   const unsigned char *bytes = reinterpret_cast<unsigned char*>(LockResource(resource));
   if (!bytes) throw PianoGameError(L"Couldn't lock TGA resource.");

   Tga *ret = LoadFromData(bytes);
   FreeResource(resource);

#else

   // Append extension on the Mac
   std::wstring full_name = WSTRING(resource_name << L".tga");
   
   CFURLRef url = CFBundleCopyResourceURL(CFBundleGetMainBundle(), MacStringFromWide(full_name).get(), 0, 0);
   if (!url) throw PianoGameError(L"Couldn't find TGA resource.");

   OSStatus status;
   CFDataRef data;
   Boolean success = CFURLCreateDataAndPropertiesFromResource(0, url, &data, 0, 0, &status);
   if (!success || status != 0) throw PianoGameError(L"Couldn't load TGA resource.");
   
   const UInt8 *bytes = CFDataGetBytePtr(data);   

   Tga *ret = LoadFromData(bytes);
   CFRelease(data);
   
#endif

   ret->SetSmooth(false);

   return ret;
}

void Tga::Release(Tga *tga)
{
   if (!tga) return;

   glDeleteTextures(1, &tga->m_texture_id);

   delete tga;
}

const static int TgaTypeHeaderLength = 12;
const static unsigned char UncompressedTgaHeader[TgaTypeHeaderLength] = {0,0,2,0,0,0,0,0,0,0,0,0};
const static unsigned char CompressedTgaHeader[TgaTypeHeaderLength] = {0,0,10,0,0,0,0,0,0,0,0,0};

void Tga::SetSmooth(bool smooth)
{
   GLint filter = GL_NEAREST;
   if (smooth) filter = GL_LINEAR;

   glBindTexture(GL_TEXTURE_2D, m_texture_id);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
}

enum TgaType
{
   TgaUncompressed,
   TgaCompressed,
   TgaUnknown
};

Tga *Tga::LoadFromData(const unsigned char *bytes)
{
   if (!bytes) return 0;

   const unsigned char *pos = bytes;

   TgaType type = TgaUnknown;
   if (memcmp(UncompressedTgaHeader, pos, TgaTypeHeaderLength) == 0) type = TgaUncompressed;
   if (memcmp(CompressedTgaHeader,   pos, TgaTypeHeaderLength) == 0) type = TgaCompressed;

   if (type == TgaUnknown)
   {
      throw PianoGameError(L"Unsupported TGA type.");
   }

   // We're done with the type header
   pos += TgaTypeHeaderLength;

   unsigned int width = pos[1] * 256 + pos[0];
   unsigned int height = pos[3] * 256 + pos[2];
   unsigned int bpp = pos[4];

   // We're done with the data header
   const static int TgaDataHeaderLength = 6;
   pos += TgaDataHeaderLength;

   if (width <= 0 || height <= 0)
   {
      throw PianoGameError(L"Invalid TGA dimensions.");
   }

   if (bpp != 24 && bpp != 32)
   {
      throw PianoGameError(L"Unsupported TGA BPP.");
   }

   const unsigned int data_size = width * height * bpp/8;
   unsigned char *image_data = new unsigned char[data_size];

   Tga *t = 0;
   if (type == TgaCompressed) t = LoadCompressed(pos, image_data, width, height, bpp);
   if (type == TgaUncompressed) t = LoadUncompressed(pos, image_data, data_size, width, height, bpp);

   delete[] image_data;
   return t;
}

Tga *Tga::LoadUncompressed(const unsigned char *src, unsigned char *dest, unsigned int size, unsigned int width, unsigned int height, unsigned int bpp)
{
   // We can use most of the data as-is with little modification
   memcpy(dest, src, size);

   for (unsigned int cswap = 0; cswap < size; cswap += bpp/8)
   {
      dest[cswap] ^= dest[cswap+2] ^= dest[cswap] ^= dest[cswap+2];
   }

   return BuildFromParameters(dest, width, height, bpp);
}

Tga *Tga::LoadCompressed(const unsigned char *src, unsigned char *dest, unsigned int width, unsigned int height, unsigned int bpp)
{
   const unsigned char *pos = src;

   const unsigned int BytesPerPixel = bpp / 8;
   const unsigned int PixelCount = height * width;

   const static unsigned int MaxBytesPerPixel = 4;
   unsigned char pixel_buffer[MaxBytesPerPixel];

   unsigned int pixel = 0;
   unsigned int byte = 0;

   while (pixel < PixelCount)
   {
      unsigned char chunkheader = 0;
      memcpy(&chunkheader, pos, sizeof(unsigned char));
      pos += sizeof(unsigned char);

      if (chunkheader < 128)
      {
         chunkheader++;
         for (short i = 0; i < chunkheader; i++)
         {
            memcpy(pixel_buffer, pos, BytesPerPixel);
            pos += BytesPerPixel;

            dest[byte + 0] = pixel_buffer[2];
            dest[byte + 1] = pixel_buffer[1];
            dest[byte + 2] = pixel_buffer[0];
            if (BytesPerPixel == 4) dest[byte + 3] = pixel_buffer[3];

            byte += BytesPerPixel;
            pixel++;

            if (pixel > PixelCount) throw PianoGameError(L"Too many pixels in TGA.");
         }
      }
      else
      {
         chunkheader -= 127;

         memcpy(pixel_buffer, pos, BytesPerPixel);
         pos += BytesPerPixel;

         for (short i = 0; i < chunkheader; i++)
         {
            dest[byte + 0] = pixel_buffer[2];
            dest[byte + 1] = pixel_buffer[1];
            dest[byte + 2] = pixel_buffer[0];
            if (BytesPerPixel == 4) dest[byte + 3] = pixel_buffer[3];

            byte += BytesPerPixel;
            pixel++;

            if (pixel > PixelCount) throw PianoGameError(L"Too many pixels in TGA.");
         }
      }
   }

   return BuildFromParameters(dest, width, height, bpp);
}


Tga *Tga::BuildFromParameters(const unsigned char *raw, unsigned int width, unsigned int height, unsigned int bpp)
{
   unsigned int pixel_format = 0;
   if (bpp == 24) pixel_format = GL_RGB;
   if (bpp == 32) pixel_format = GL_RGBA;

   TextureId id;
   glGenTextures(1, &id);
   if (!id) return 0;

   glBindTexture(GL_TEXTURE_2D, id);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
   glTexImage2D(GL_TEXTURE_2D, 0, bpp/8, width, height, 0, pixel_format, GL_UNSIGNED_BYTE, raw);

   Tga *t = new Tga();
   t->m_width = width;
   t->m_height = height;
   t->m_texture_id = id;

   return t;
}
