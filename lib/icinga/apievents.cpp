/******************************************************************************
 * Icinga 2                                                                   *
 * Copyright (C) 2012-2014 Icinga Development Team (http://www.icinga.org)    *
 *                                                                            *
 * This program is free software; you can redistribute it and/or              *
 * modify it under the terms of the GNU General Public License                *
 * as published by the Free Software Foundation; either version 2             *
 * of the License, or (at your option) any later version.                     *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program; if not, write to the Free Software Foundation     *
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.             *
 ******************************************************************************/

#include "icinga/apievents.hpp"
#include "icinga/service.hpp"
#include "remote/apilistener.hpp"
#include "remote/endpoint.hpp"
#include "remote/messageorigin.hpp"
#include "remote/zone.hpp"
#include "remote/apifunction.hpp"
#include "base/application.hpp"
#include "base/dynamictype.hpp"
#include "base/utility.hpp"
#include "base/exception.hpp"
#include "base/initialize.hpp"
#include <fstream>

using namespace icinga;

INITIALIZE_ONCE(&ApiEvents::StaticInitialize);

REGISTER_APIFUNCTION(CheckResult, event, &ApiEvents::CheckResultAPIHandler);
REGISTER_APIFUNCTION(SetNextCheck, event, &ApiEvents::NextCheckChangedAPIHandler);
REGISTER_APIFUNCTION(SetNextNotification, event, &ApiEvents::NextNotificationChangedAPIHandler);
REGISTER_APIFUNCTION(SetForceNextCheck, event, &ApiEvents::ForceNextCheckChangedAPIHandler);
REGISTER_APIFUNCTION(SetForceNextNotification, event, &ApiEvents::ForceNextNotificationChangedAPIHandler);
REGISTER_APIFUNCTION(SetEnableActiveChecks, event, &ApiEvents::EnableActiveChecksChangedAPIHandler);
REGISTER_APIFUNCTION(SetEnablePassiveChecks, event, &ApiEvents::EnablePassiveChecksChangedAPIHandler);
REGISTER_APIFUNCTION(SetEnableNotifications, event, &ApiEvents::EnableNotificationsChangedAPIHandler);
REGISTER_APIFUNCTION(SetEnableFlapping, event, &ApiEvents::EnableFlappingChangedAPIHandler);
REGISTER_APIFUNCTION(AddComment, event, &ApiEvents::CommentAddedAPIHandler);
REGISTER_APIFUNCTION(RemoveComment, event, &ApiEvents::CommentRemovedAPIHandler);
REGISTER_APIFUNCTION(AddDowntime, event, &ApiEvents::DowntimeAddedAPIHandler);
REGISTER_APIFUNCTION(RemoveDowntime, event, &ApiEvents::DowntimeRemovedAPIHandler);
REGISTER_APIFUNCTION(SetAcknowledgement, event, &ApiEvents::AcknowledgementSetAPIHandler);
REGISTER_APIFUNCTION(ClearAcknowledgement, event, &ApiEvents::AcknowledgementClearedAPIHandler);
REGISTER_APIFUNCTION(UpdateRepository, event, &ApiEvents::UpdateRepositoryAPIHandler);

static Timer::Ptr l_RepositoryTimer;

void ApiEvents::StaticInitialize(void)
{
	Checkable::OnNewCheckResult.connect(&ApiEvents::CheckResultHandler);
	Checkable::OnNextCheckChanged.connect(&ApiEvents::NextCheckChangedHandler);
	Notification::OnNextNotificationChanged.connect(&ApiEvents::NextNotificationChangedHandler);
	Checkable::OnForceNextCheckChanged.connect(&ApiEvents::ForceNextCheckChangedHandler);
	Checkable::OnForceNextNotificationChanged.connect(&ApiEvents::ForceNextNotificationChangedHandler);
	Checkable::OnEnableActiveChecksChanged.connect(&ApiEvents::EnableActiveChecksChangedHandler);
	Checkable::OnEnablePassiveChecksChanged.connect(&ApiEvents::EnablePassiveChecksChangedHandler);
	Checkable::OnEnableNotificationsChanged.connect(&ApiEvents::EnableNotificationsChangedHandler);
	Checkable::OnEnableFlappingChanged.connect(&ApiEvents::EnableFlappingChangedHandler);
	Checkable::OnCommentAdded.connect(&ApiEvents::CommentAddedHandler);
	Checkable::OnCommentRemoved.connect(&ApiEvents::CommentRemovedHandler);
	Checkable::OnDowntimeAdded.connect(&ApiEvents::DowntimeAddedHandler);
	Checkable::OnDowntimeRemoved.connect(&ApiEvents::DowntimeRemovedHandler);
	Checkable::OnAcknowledgementSet.connect(&ApiEvents::AcknowledgementSetHandler);
	Checkable::OnAcknowledgementCleared.connect(&ApiEvents::AcknowledgementClearedHandler);

	l_RepositoryTimer = make_shared<Timer>();
	l_RepositoryTimer->SetInterval(30);
	l_RepositoryTimer->OnTimerExpired.connect(boost::bind(&ApiEvents::RepositoryTimerHandler));
	l_RepositoryTimer->Start();
	l_RepositoryTimer->Reschedule(0);
}

void ApiEvents::CheckResultHandler(const Checkable::Ptr& checkable, const CheckResult::Ptr& cr, const MessageOrigin& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Dictionary::Ptr message = make_shared<Dictionary>();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::CheckResult");

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = make_shared<Dictionary>();
	params->Set("host", GetVirtualHostName(host));
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("cr", Serialize(cr));

	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::CheckResultAPIHandler(const MessageOrigin& origin, const Dictionary::Ptr& params)
{
	if (!params)
		return Empty;

	Value crv = params->Get("cr");

	CheckResult::Ptr cr = Deserialize(crv, true);

	Host::Ptr host = FindHostByVirtualName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (!origin.FromZone || !origin.FromZone->CanAccessObject(checkable))
		return Empty;

	checkable->ProcessCheckResult(cr, origin);

	return Empty;
}

void ApiEvents::NextCheckChangedHandler(const Checkable::Ptr& checkable, double nextCheck, const MessageOrigin& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = make_shared<Dictionary>();
	params->Set("host", GetVirtualHostName(host));
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("next_check", nextCheck);

	Dictionary::Ptr message = make_shared<Dictionary>();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetNextCheck");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::NextCheckChangedAPIHandler(const MessageOrigin& origin, const Dictionary::Ptr& params)
{
	if (!params)
		return Empty;

	Host::Ptr host = FindHostByVirtualName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (!origin.FromZone || !origin.FromZone->CanAccessObject(checkable))
		return Empty;

	checkable->SetNextCheck(params->Get("next_check"), origin);

	return Empty;
}

void ApiEvents::NextNotificationChangedHandler(const Notification::Ptr& notification, double nextNotification, const MessageOrigin& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Dictionary::Ptr params = make_shared<Dictionary>();
	params->Set("notification", notification->GetName());
	params->Set("next_notification", nextNotification);

	Dictionary::Ptr message = make_shared<Dictionary>();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetNextNotification");
	message->Set("params", params);

	listener->RelayMessage(origin, notification, message, true);
}

Value ApiEvents::NextNotificationChangedAPIHandler(const MessageOrigin& origin, const Dictionary::Ptr& params)
{
	if (!params)
		return Empty;

	Notification::Ptr notification = Notification::GetByName(params->Get("notification"));

	if (!notification)
		return Empty;

	if (!origin.FromZone || !origin.FromZone->CanAccessObject(notification))
		return Empty;

	notification->SetNextNotification(params->Get("next_notification"), origin);

	return Empty;
}

void ApiEvents::ForceNextCheckChangedHandler(const Checkable::Ptr& checkable, bool forced, const MessageOrigin& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = make_shared<Dictionary>();
	params->Set("host", GetVirtualHostName(host));
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("forced", forced);

	Dictionary::Ptr message = make_shared<Dictionary>();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetForceNextCheck");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::ForceNextCheckChangedAPIHandler(const MessageOrigin& origin, const Dictionary::Ptr& params)
{
	if (!params)
		return Empty;

	Host::Ptr host = FindHostByVirtualName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (!origin.FromZone || !origin.FromZone->CanAccessObject(checkable))
		return Empty;

	checkable->SetForceNextCheck(params->Get("forced"), origin);

	return Empty;
}

void ApiEvents::ForceNextNotificationChangedHandler(const Checkable::Ptr& checkable, bool forced, const MessageOrigin& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = make_shared<Dictionary>();
	params->Set("host", GetVirtualHostName(host));
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("forced", forced);

	Dictionary::Ptr message = make_shared<Dictionary>();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetForceNextNotification");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::ForceNextNotificationChangedAPIHandler(const MessageOrigin& origin, const Dictionary::Ptr& params)
{
	if (!params)
		return Empty;

	Host::Ptr host = FindHostByVirtualName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (!origin.FromZone || !origin.FromZone->CanAccessObject(checkable))
		return Empty;

	checkable->SetForceNextNotification(params->Get("forced"), origin);

	return Empty;
}

void ApiEvents::EnableActiveChecksChangedHandler(const Checkable::Ptr& checkable, bool enabled, const MessageOrigin& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = make_shared<Dictionary>();
	params->Set("host", GetVirtualHostName(host));
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("enabled", enabled);

	Dictionary::Ptr message = make_shared<Dictionary>();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetEnableActiveChecks");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::EnableActiveChecksChangedAPIHandler(const MessageOrigin& origin, const Dictionary::Ptr& params)
{
	if (!params)
		return Empty;

	Host::Ptr host = FindHostByVirtualName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (!origin.FromZone || !origin.FromZone->CanAccessObject(checkable))
		return Empty;

	checkable->SetEnableActiveChecks(params->Get("enabled"), origin);

	return Empty;
}

void ApiEvents::EnablePassiveChecksChangedHandler(const Checkable::Ptr& checkable, bool enabled, const MessageOrigin& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = make_shared<Dictionary>();
	params->Set("host", GetVirtualHostName(host));
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("enabled", enabled);

	Dictionary::Ptr message = make_shared<Dictionary>();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetEnablePassiveChecks");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::EnablePassiveChecksChangedAPIHandler(const MessageOrigin& origin, const Dictionary::Ptr& params)
{
	if (!params)
		return Empty;

	Host::Ptr host = FindHostByVirtualName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (!origin.FromZone || !origin.FromZone->CanAccessObject(checkable))
		return Empty;

	checkable->SetEnablePassiveChecks(params->Get("enabled"), origin);

	return Empty;
}

void ApiEvents::EnableNotificationsChangedHandler(const Checkable::Ptr& checkable, bool enabled, const MessageOrigin& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = make_shared<Dictionary>();
	params->Set("host", GetVirtualHostName(host));
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("enabled", enabled);

	Dictionary::Ptr message = make_shared<Dictionary>();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetEnableNotifications");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::EnableNotificationsChangedAPIHandler(const MessageOrigin& origin, const Dictionary::Ptr& params)
{
	if (!params)
		return Empty;

	Host::Ptr host = FindHostByVirtualName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (!origin.FromZone || !origin.FromZone->CanAccessObject(checkable))
		return Empty;

	checkable->SetEnableNotifications(params->Get("enabled"), origin);

	return Empty;
}

void ApiEvents::EnableFlappingChangedHandler(const Checkable::Ptr& checkable, bool enabled, const MessageOrigin& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = make_shared<Dictionary>();
	params->Set("host", GetVirtualHostName(host));
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("enabled", enabled);

	Dictionary::Ptr message = make_shared<Dictionary>();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetEnableFlapping");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::EnableFlappingChangedAPIHandler(const MessageOrigin& origin, const Dictionary::Ptr& params)
{
	if (!params)
		return Empty;

	Host::Ptr host = FindHostByVirtualName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (!origin.FromZone || !origin.FromZone->CanAccessObject(checkable))
		return Empty;

	checkable->SetEnableFlapping(params->Get("enabled"), origin);

	return Empty;
}

void ApiEvents::CommentAddedHandler(const Checkable::Ptr& checkable, const Comment::Ptr& comment, const MessageOrigin& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = make_shared<Dictionary>();
	params->Set("host", GetVirtualHostName(host));
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("comment", Serialize(comment));

	Dictionary::Ptr message = make_shared<Dictionary>();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::AddComment");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::CommentAddedAPIHandler(const MessageOrigin& origin, const Dictionary::Ptr& params)
{
	if (!params)
		return Empty;

	Host::Ptr host = FindHostByVirtualName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (!origin.FromZone || !origin.FromZone->CanAccessObject(checkable))
		return Empty;

	Comment::Ptr comment = Deserialize(params->Get("comment"), true);

	checkable->AddComment(comment->GetEntryType(), comment->GetAuthor(),
	    comment->GetText(), comment->GetExpireTime(), comment->GetId(), origin);

	return Empty;
}

void ApiEvents::CommentRemovedHandler(const Checkable::Ptr& checkable, const Comment::Ptr& comment, const MessageOrigin& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = make_shared<Dictionary>();
	params->Set("host", GetVirtualHostName(host));
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("id", comment->GetId());

	Dictionary::Ptr message = make_shared<Dictionary>();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::RemoveComment");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::CommentRemovedAPIHandler(const MessageOrigin& origin, const Dictionary::Ptr& params)
{
	if (!params)
		return Empty;

	Host::Ptr host = FindHostByVirtualName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (!origin.FromZone || !origin.FromZone->CanAccessObject(checkable))
		return Empty;

	checkable->RemoveComment(params->Get("id"), origin);

	return Empty;
}

void ApiEvents::DowntimeAddedHandler(const Checkable::Ptr& checkable, const Downtime::Ptr& downtime, const MessageOrigin& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = make_shared<Dictionary>();
	params->Set("host", GetVirtualHostName(host));
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("downtime", Serialize(downtime));

	Dictionary::Ptr message = make_shared<Dictionary>();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::AddDowntime");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::DowntimeAddedAPIHandler(const MessageOrigin& origin, const Dictionary::Ptr& params)
{
	if (!params)
		return Empty;

	Host::Ptr host = FindHostByVirtualName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (!origin.FromZone || !origin.FromZone->CanAccessObject(checkable))
		return Empty;

	Downtime::Ptr downtime = Deserialize(params->Get("downtime"), true);

	checkable->AddDowntime(downtime->GetAuthor(), downtime->GetComment(),
	    downtime->GetStartTime(), downtime->GetEndTime(),
	    downtime->GetFixed(), downtime->GetTriggeredBy(),
	    downtime->GetDuration(), downtime->GetScheduledBy(),
	    downtime->GetId(), origin);

	return Empty;
}

void ApiEvents::DowntimeRemovedHandler(const Checkable::Ptr& checkable, const Downtime::Ptr& downtime, const MessageOrigin& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = make_shared<Dictionary>();
	params->Set("host", GetVirtualHostName(host));
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("id", downtime->GetId());

	Dictionary::Ptr message = make_shared<Dictionary>();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::RemoveDowntime");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::DowntimeRemovedAPIHandler(const MessageOrigin& origin, const Dictionary::Ptr& params)
{
	if (!params)
		return Empty;

	Host::Ptr host = FindHostByVirtualName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (!origin.FromZone || !origin.FromZone->CanAccessObject(checkable))
		return Empty;

	checkable->RemoveDowntime(params->Get("id"), false, origin);

	return Empty;
}

void ApiEvents::AcknowledgementSetHandler(const Checkable::Ptr& checkable,
    const String& author, const String& comment, AcknowledgementType type,
    double expiry, const MessageOrigin& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = make_shared<Dictionary>();
	params->Set("host", GetVirtualHostName(host));
	if (service)
		params->Set("service", service->GetShortName());
	params->Set("author", author);
	params->Set("comment", comment);
	params->Set("acktype", type);
	params->Set("expiry", expiry);

	Dictionary::Ptr message = make_shared<Dictionary>();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::SetAcknowledgement");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::AcknowledgementSetAPIHandler(const MessageOrigin& origin, const Dictionary::Ptr& params)
{
	if (!params)
		return Empty;

	Host::Ptr host = FindHostByVirtualName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (!origin.FromZone || !origin.FromZone->CanAccessObject(checkable))
		return Empty;

	checkable->AcknowledgeProblem(params->Get("author"), params->Get("comment"),
	    static_cast<AcknowledgementType>(static_cast<int>(params->Get("acktype"))),
	    params->Get("expiry"), origin);

	return Empty;
}

void ApiEvents::AcknowledgementClearedHandler(const Checkable::Ptr& checkable, const MessageOrigin& origin)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	Dictionary::Ptr params = make_shared<Dictionary>();
	params->Set("host", GetVirtualHostName(host));
	if (service)
		params->Set("service", service->GetShortName());

	Dictionary::Ptr message = make_shared<Dictionary>();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::ClearAcknowledgement");
	message->Set("params", params);

	listener->RelayMessage(origin, checkable, message, true);
}

Value ApiEvents::AcknowledgementClearedAPIHandler(const MessageOrigin& origin, const Dictionary::Ptr& params)
{
	if (!params)
		return Empty;

	Host::Ptr host = FindHostByVirtualName(params->Get("host"));

	if (!host)
		return Empty;

	Checkable::Ptr checkable;

	if (params->Contains("service"))
		checkable = host->GetServiceByShortName(params->Get("service"));
	else
		checkable = host;

	if (!checkable)
		return Empty;

	if (!origin.FromZone || !origin.FromZone->CanAccessObject(checkable))
		return Empty;

	checkable->ClearAcknowledgement(origin);

	return Empty;
}

void ApiEvents::RepositoryTimerHandler(void)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return;

	Dictionary::Ptr repository = make_shared<Dictionary>();

	BOOST_FOREACH(const Host::Ptr& host, DynamicType::GetObjects<Host>()) {
		Array::Ptr services = make_shared<Array>();

		BOOST_FOREACH(const Service::Ptr& service, host->GetServices()) {
			services->Add(service->GetShortName());
		}

		repository->Set(GetVirtualHostName(host), services);
	}

	Endpoint::Ptr my_endpoint = Endpoint::GetLocalEndpoint();
	Zone::Ptr my_zone = my_endpoint->GetZone();

	Dictionary::Ptr params = make_shared<Dictionary>();
	params->Set("seen", Utility::GetTime());
	params->Set("endpoint", my_endpoint->GetName());

	Zone::Ptr parent_zone = my_zone->GetParent();
	if (parent_zone)
		params->Set("parent_zone", parent_zone->GetName());

	params->Set("zone", my_zone->GetName());
	params->Set("repository", repository);

	Dictionary::Ptr message = make_shared<Dictionary>();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::UpdateRepository");
	message->Set("params", params);

	listener->RelayMessage(MessageOrigin(), my_zone, message, false);
}

String ApiEvents::GetRepositoryDir(void)
{
	return Application::GetLocalStateDir() + "/lib/icinga2/api/repository/";
}

Value ApiEvents::UpdateRepositoryAPIHandler(const MessageOrigin& origin, const Dictionary::Ptr& params)
{
	if (!params)
		return Empty;

	String repositoryFile = GetRepositoryDir() + SHA256(params->Get("endpoint"));
	String repositoryTempFile = repositoryFile + ".tmp";

	std::ofstream fp(repositoryTempFile.CStr(), std::ofstream::out | std::ostream::trunc);
	fp << JsonSerialize(params);
	fp.close();

#ifdef _WIN32
	_unlink(repositoryFile.CStr());
#endif /* _WIN32 */

	if (rename(repositoryTempFile.CStr(), repositoryFile.CStr()) < 0) {
		BOOST_THROW_EXCEPTION(posix_error()
		    << boost::errinfo_api_function("rename")
		    << boost::errinfo_errno(errno)
		    << boost::errinfo_file_name(repositoryTempFile));
	}

	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return Empty;

	Dictionary::Ptr message = make_shared<Dictionary>();
	message->Set("jsonrpc", "2.0");
	message->Set("method", "event::UpdateRepository");
	message->Set("params", params);

	listener->RelayMessage(origin, Zone::GetLocalZone(), message, true);

	return Empty;
}

String ApiEvents::GetVirtualHostName(const Host::Ptr& host)
{
	String host_name = host->GetName();
	if (host_name == "localhost")
		host_name = Endpoint::GetLocalEndpoint()->GetName();
	return host_name;
}

Host::Ptr ApiEvents::FindHostByVirtualName(const String& hostName)
{
	if (hostName == Endpoint::GetLocalEndpoint()->GetName())
		return Host::GetByName("localhost");
	else
		return Host::GetByName(hostName);
}
