// EventKit glue (Objective-C++, compiled with ARC). See calendar.hpp.
#import <AppKit/AppKit.h>
#import <EventKit/EventKit.h>
#import <Foundation/Foundation.h>

#include <algorithm>

#include "calendar.hpp"

namespace rma {

struct CalendarSource::Impl {
  EKEventStore* store = nil;
  id observer = nil;
};

namespace {

std::string str(NSString* s) { return s ? std::string(s.UTF8String) : std::string(); }

CalendarSource::Access current_access() {
  EKAuthorizationStatus st = [EKEventStore authorizationStatusForEntityType:EKEntityTypeEvent];
  if (st == EKAuthorizationStatusFullAccess) return CalendarSource::Access::Granted;
  if (st == EKAuthorizationStatusWriteOnly) return CalendarSource::Access::Denied;  // can't read
  if (st == EKAuthorizationStatusDenied || st == EKAuthorizationStatusRestricted) {
    return CalendarSource::Access::Denied;
  }
  return CalendarSource::Access::Unknown;
}

}  // namespace

CalendarSource::CalendarSource() : impl_(std::make_unique<Impl>()) {
  impl_->store = [[EKEventStore alloc] init];
  access_ = current_access();
  std::atomic<bool>* changed = &changed_;
  impl_->observer = [[NSNotificationCenter defaultCenter]
      addObserverForName:EKEventStoreChangedNotification
                  object:impl_->store
                   queue:nil
              usingBlock:^(NSNotification*) {
                changed->store(true);
              }];
}

CalendarSource::~CalendarSource() {
  if (impl_->observer) [[NSNotificationCenter defaultCenter] removeObserver:impl_->observer];
}

void CalendarSource::request_access() {
  if (access_ == Access::Granted || access_ == Access::Requesting) return;
  access_ = Access::Requesting;
  std::atomic<Access>* access = &access_;
  std::atomic<bool>* changed = &changed_;
  EKEventStore* store = impl_->store;
  // The completion runs on an arbitrary queue: only touch the atomics there.
  auto done = ^(BOOL granted, NSError*) {
    if (granted) [store reset];  // a store made before the grant sees nothing until reset
    access->store(granted ? Access::Granted : Access::Denied);
    changed->store(true);
  };
  [store requestFullAccessToEventsWithCompletion:done];  // macOS 14+
}

std::vector<CalendarInfo> CalendarSource::calendars() {
  std::vector<CalendarInfo> out;
  if (access_ != Access::Granted) return out;
  for (EKCalendar* c in [impl_->store calendarsForEntityType:EKEntityTypeEvent]) {
    CalendarInfo info;
    info.id = str(c.calendarIdentifier);
    info.title = str(c.title);
    info.account = str(c.source.title);
    NSColor* col = [c.color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    if (col) {
      info.rgb[0] = static_cast<float>(col.redComponent);
      info.rgb[1] = static_cast<float>(col.greenComponent);
      info.rgb[2] = static_cast<float>(col.blueComponent);
    }
    out.push_back(info);
  }
  std::sort(out.begin(), out.end(), [](const CalendarInfo& a, const CalendarInfo& b) {
    return a.account != b.account ? a.account < b.account : a.title < b.title;
  });
  return out;
}

std::vector<rma3::CalEvent> CalendarSource::events(const std::vector<std::string>& calendar_ids, double from,
                                                   double to) {
  std::vector<rma3::CalEvent> out;
  if (access_ != Access::Granted || calendar_ids.empty()) return out;
  NSMutableArray<EKCalendar*>* cals = [NSMutableArray array];
  for (const std::string& cid : calendar_ids) {
    EKCalendar* c = [impl_->store calendarWithIdentifier:[NSString stringWithUTF8String:cid.c_str()]];
    if (c) [cals addObject:c];
  }
  if (cals.count == 0) return out;
  NSPredicate* pred = [impl_->store predicateForEventsWithStartDate:[NSDate dateWithTimeIntervalSince1970:from]
                                                            endDate:[NSDate dateWithTimeIntervalSince1970:to]
                                                          calendars:cals];
  for (EKEvent* e in [impl_->store eventsMatchingPredicate:pred]) {
    rma3::CalEvent ev;
    NSString* ident = e.eventIdentifier ?: e.calendarItemIdentifier;
    ev.id = str(ident);
    ev.start = e.startDate.timeIntervalSince1970;
    ev.end = e.endDate.timeIntervalSince1970;
    ev.all_day = e.allDay;
    ev.free = e.availability == EKEventAvailabilityFree;
    ev.cancelled = e.status == EKEventStatusCanceled;
    for (EKParticipant* p in e.attendees) {
      if (p.isCurrentUser && p.participantStatus == EKParticipantStatusDeclined) ev.declined = true;
    }
    out.push_back(ev);
  }
  return out;
}

}  // namespace rma
