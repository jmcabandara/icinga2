/******************************************************************************
 * Icinga 2                                                                   *
 * Copyright (C) 2012-2015 Icinga Development Team (http://www.icinga.org)    *
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

#ifndef SCRIPTFUNCTION_H
#define SCRIPTFUNCTION_H

#include "base/i2-base.hpp"
#include "base/value.hpp"
#include "base/functionwrapper.hpp"
#include "base/scriptglobal.hpp"
#include <vector>
#include <boost/function.hpp>

namespace icinga
{

/**
 * A script function that can be used to execute a script task.
 *
 * @ingroup base
 */
class I2_BASE_API Function : public Object
{
public:
	DECLARE_OBJECT(Function);

	typedef boost::function<Value (const std::vector<Value>& arguments)> Callback;

	Function(const Callback& function);

	Value Invoke(const std::vector<Value>& arguments);

	static Object::Ptr GetPrototype(void);

private:
	Callback m_Callback;
};

#define REGISTER_SCRIPTFUNCTION(name, callback) \
	namespace { namespace UNIQUE_NAME(sf) { namespace sf ## name { \
		void RegisterFunction(void) { \
			Function::Ptr sf = new icinga::Function(WrapFunction(callback)); \
			ScriptGlobal::Set(#name, sf); \
		} \
		INITIALIZE_ONCE(RegisterFunction); \
	} } }

}

#endif /* SCRIPTFUNCTION_H */
