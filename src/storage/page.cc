#include "storage/page.h"

#include <cstring>

namespace db {

Page::Page() : Page(0) {}

Page::Page(uint32_t page_id) : bytes_(kPageSize, 0) { initialize(page_id); }

Page Page::from_bytes(const uint8_t *raw_bytes) {
  if (raw_bytes == nullptr) {
    throw StorageException("Null page bytes");
  }
  Page page(0);
  std::memcpy(page.bytes_.data(), raw_bytes, kPageSize);
  return page;
}

void Page::initialize(uint32_t page_id) {
  std::memset(bytes_.data(), 0, kPageSize);
  PageHeader &hdr = header();
  hdr.page_id = page_id;
  hdr.slot_count = 0;
  hdr.lower = static_cast<uint16_t>(sizeof(PageHeader));
  hdr.upper = static_cast<uint16_t>(kPageSize);
  hdr.flags = 0;
}

uint32_t Page::get_page_id() const { return header().page_id; }

uint16_t Page::get_slot_count() const { return header().slot_count; }

size_t Page::get_free_space() const {
  const PageHeader &hdr = header();
  if (hdr.upper < hdr.lower) {
    return 0;
  }
  return static_cast<size_t>(hdr.upper - hdr.lower);
}

const uint8_t *Page::data() const { return bytes_.data(); }

uint8_t *Page::mutable_data() { return bytes_.data(); }

uint16_t Page::insert_tuple(const uint8_t *tuple_bytes,
                            uint16_t tuple_length) {
  if (tuple_bytes == nullptr) {
    throw StorageException("Null tuple bytes");
  }
  const size_t needed =
      static_cast<size_t>(tuple_length) + sizeof(SlotEntry);
  if (get_free_space() < needed) {
    throw StorageException("Tuple does not fit on page");
  }
  PageHeader &hdr = header();
  const uint16_t offset = hdr.lower;
  std::memcpy(bytes_.data() + offset, tuple_bytes, tuple_length);
  hdr.lower =
      static_cast<uint16_t>(hdr.lower + static_cast<uint16_t>(tuple_length));
  hdr.upper =
      static_cast<uint16_t>(hdr.upper - static_cast<uint16_t>(sizeof(SlotEntry)));
  SlotEntry *slot = reinterpret_cast<SlotEntry *>(bytes_.data() + hdr.upper);
  slot->offset = offset;
  slot->length = tuple_length;
  const uint16_t slot_index = hdr.slot_count;
  hdr.slot_count =
      static_cast<uint16_t>(hdr.slot_count + static_cast<uint16_t>(1));
  return slot_index;
}

std::optional<std::vector<uint8_t>> Page::read_tuple(uint16_t slot) const {
  if (!is_slot_live(slot)) {
    return std::nullopt;
  }
  const SlotEntry *entry = slot_at(slot);
  std::vector<uint8_t> tuple(entry->length);
  std::memcpy(tuple.data(), bytes_.data() + entry->offset, entry->length);
  return tuple;
}

void Page::delete_tuple(uint16_t slot) {
  if (slot >= header().slot_count) {
    throw StorageException("Invalid slot index");
  }
  SlotEntry *entry = slot_at(slot);
  entry->offset = 0;
  entry->length = 0;
}

bool Page::update_tuple_in_place(uint16_t slot, const uint8_t *tuple_bytes,
                                 uint16_t tuple_length) {
  if (tuple_bytes == nullptr || !is_slot_live(slot)) {
    return false;
  }
  SlotEntry *entry = slot_at(slot);
  if (entry->length != tuple_length) {
    return false;
  }
  std::memcpy(bytes_.data() + entry->offset, tuple_bytes, tuple_length);
  return true;
}

bool Page::is_slot_live(uint16_t slot) const {
  if (slot >= header().slot_count) {
    return false;
  }
  const SlotEntry *entry = slot_at(slot);
  return entry->offset != 0 && entry->length != 0;
}

PageHeader &Page::header() {
  return *reinterpret_cast<PageHeader *>(bytes_.data());
}

const PageHeader &Page::header() const {
  return *reinterpret_cast<const PageHeader *>(bytes_.data());
}

SlotEntry *Page::slot_at(uint16_t index) {
  const PageHeader &hdr = header();
  const size_t base =
      static_cast<size_t>(hdr.upper) +
      static_cast<size_t>(hdr.slot_count - 1 - index) * sizeof(SlotEntry);
  return reinterpret_cast<SlotEntry *>(bytes_.data() + base);
}

const SlotEntry *Page::slot_at(uint16_t index) const {
  const PageHeader &hdr = header();
  const size_t base =
      static_cast<size_t>(hdr.upper) +
      static_cast<size_t>(hdr.slot_count - 1 - index) * sizeof(SlotEntry);
  return reinterpret_cast<const SlotEntry *>(bytes_.data() + base);
}

}  // namespace db
