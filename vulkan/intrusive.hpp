#pragma once

#include <utility>
#include <stddef.h>

struct IntrusivePtrEnabled
{
   size_t reference_count = 1;
   IntrusivePtrEnabled() = default;
   IntrusivePtrEnabled(const IntrusivePtrEnabled &) = delete;
   void operator=(const IntrusivePtrEnabled &) = delete;
};

template <typename T>
class IntrusivePtr
{
public:
   IntrusivePtr() = default;
   IntrusivePtr(T *handle)
      : data(handle)
   {
   }

   operator T&()
   {
      return *data;
   }

   operator const T&() const
   {
      return *data;
   }

   explicit operator bool() const
   {
      return data != nullptr;
   }

   T *get()
   {
      return data;
   }

   const T *get() const
   {
      return data;
   }

   T *release()
   {
      if (!data)
         return nullptr;

      unsigned count = --data->reference_count;
      if (count == 0)
         delete data;
      auto ret = data;
      data = nullptr;
      return ret;
   }

   IntrusivePtr &operator=(const IntrusivePtr &other)
   {
      if (this != &other)
      {
         release();
         data = other.data;
         data->reference_count++;
      }
      return *this;
   }

   IntrusivePtr(const IntrusivePtr &other)
   {
      *this = other;
   }

   ~IntrusivePtr()
   {
      release();
   }

   IntrusivePtr &operator=(IntrusivePtr &&other)
   {
      if (this != &other)
      {
         release();
         data = other.data;
         other.data = nullptr;
      }
      return *this;
   }

   IntrusivePtr(IntrusivePtr &&other)
   {
      *this = std::move(other);
   }

private:
   T *data = nullptr;
};

template <typename T, typename... P>
IntrusivePtr<T> make_handle(P&&... p)
{
   return IntrusivePtr<T>(new T(std::forward<P>(p)...));
}

