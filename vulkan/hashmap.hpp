#pragma once
#include <stdint.h>
#include <unordered_map>
#include <memory>

namespace Vulkan
{
   using Hash = uint64_t;

   template <typename T>
   class HashMap
   {
      public:
         T *find(Hash hash)
         {
            auto itr = hashmap.find(hash);
            if (itr == end(hashmap))
               return nullptr;
            else
               return itr->second.get();
         }

         void insert(Hash hash, T *obj)
         {
            hashmap[hash] = std::unique_ptr<T>(obj);
         }

      private:
         std::unordered_map<Hash, std::unique_ptr<T>> hashmap;
   };
}
