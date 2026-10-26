#pragma once

#include <mfapi.h>
#include <mfidl.h>
#include <wrl/client.h>

// Forwards every IMFAttributes method to `attributes_` (a ComPtr<IMFAttributes>
// member of the class that expands this macro).
#define WEBICAM_IMFATTRIBUTES_FORWARD                                                                          \
  STDMETHODIMP GetItem(REFGUID k, PROPVARIANT* v) override { return attributes_->GetItem(k, v); }               \
  STDMETHODIMP GetItemType(REFGUID k, MF_ATTRIBUTE_TYPE* t) override { return attributes_->GetItemType(k, t); } \
  STDMETHODIMP CompareItem(REFGUID k, REFPROPVARIANT v, BOOL* r) override { return attributes_->CompareItem(k, v, r); } \
  STDMETHODIMP Compare(IMFAttributes* o, MF_ATTRIBUTES_MATCH_TYPE t, BOOL* r) override {                       \
    return attributes_->Compare(o, t, r);                                                                       \
  }                                                                                                             \
  STDMETHODIMP GetUINT32(REFGUID k, UINT32* v) override { return attributes_->GetUINT32(k, v); }                \
  STDMETHODIMP GetUINT64(REFGUID k, UINT64* v) override { return attributes_->GetUINT64(k, v); }                \
  STDMETHODIMP GetDouble(REFGUID k, double* v) override { return attributes_->GetDouble(k, v); }                \
  STDMETHODIMP GetGUID(REFGUID k, GUID* v) override { return attributes_->GetGUID(k, v); }                      \
  STDMETHODIMP GetStringLength(REFGUID k, UINT32* v) override { return attributes_->GetStringLength(k, v); }    \
  STDMETHODIMP GetString(REFGUID k, LPWSTR s, UINT32 n, UINT32* l) override { return attributes_->GetString(k, s, n, l); } \
  STDMETHODIMP GetAllocatedString(REFGUID k, LPWSTR* s, UINT32* l) override {                                   \
    return attributes_->GetAllocatedString(k, s, l);                                                            \
  }                                                                                                             \
  STDMETHODIMP GetBlobSize(REFGUID k, UINT32* v) override { return attributes_->GetBlobSize(k, v); }            \
  STDMETHODIMP GetBlob(REFGUID k, UINT8* b, UINT32 n, UINT32* l) override { return attributes_->GetBlob(k, b, n, l); } \
  STDMETHODIMP GetAllocatedBlob(REFGUID k, UINT8** b, UINT32* l) override { return attributes_->GetAllocatedBlob(k, b, l); } \
  STDMETHODIMP GetUnknown(REFGUID k, REFIID i, LPVOID* p) override { return attributes_->GetUnknown(k, i, p); }  \
  STDMETHODIMP SetItem(REFGUID k, REFPROPVARIANT v) override { return attributes_->SetItem(k, v); }             \
  STDMETHODIMP DeleteItem(REFGUID k) override { return attributes_->DeleteItem(k); }                            \
  STDMETHODIMP DeleteAllItems() override { return attributes_->DeleteAllItems(); }                              \
  STDMETHODIMP SetUINT32(REFGUID k, UINT32 v) override { return attributes_->SetUINT32(k, v); }                 \
  STDMETHODIMP SetUINT64(REFGUID k, UINT64 v) override { return attributes_->SetUINT64(k, v); }                 \
  STDMETHODIMP SetDouble(REFGUID k, double v) override { return attributes_->SetDouble(k, v); }                 \
  STDMETHODIMP SetGUID(REFGUID k, REFGUID v) override { return attributes_->SetGUID(k, v); }                    \
  STDMETHODIMP SetString(REFGUID k, LPCWSTR v) override { return attributes_->SetString(k, v); }                \
  STDMETHODIMP SetBlob(REFGUID k, const UINT8* b, UINT32 n) override { return attributes_->SetBlob(k, b, n); }  \
  STDMETHODIMP SetUnknown(REFGUID k, IUnknown* u) override { return attributes_->SetUnknown(k, u); }            \
  STDMETHODIMP LockStore() override { return attributes_->LockStore(); }                                        \
  STDMETHODIMP UnlockStore() override { return attributes_->UnlockStore(); }                                    \
  STDMETHODIMP GetCount(UINT32* n) override { return attributes_->GetCount(n); }                                \
  STDMETHODIMP GetItemByIndex(UINT32 i, GUID* k, PROPVARIANT* v) override { return attributes_->GetItemByIndex(i, k, v); } \
  STDMETHODIMP CopyAllItems(IMFAttributes* d) override { return attributes_->CopyAllItems(d); }
