#pragma once

/************************************************************************/
/*
Author:	mailto:gongyiling@myhada.com
Date:	2011/5/4
tracker
*/
/************************************************************************/

#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>

class tracker
{
public:

	typedef boost::shared_ptr<char> shared_refer;

	typedef boost::weak_ptr<char> weak_refer;

	tracker(void):m_refer(new char){}

	~tracker(void){}

	const weak_refer get(){return weak_refer(m_refer);}

	void set(){m_refer.reset(new char);}

	void reset(){m_refer.reset();}

	bool is_null(){return m_refer.use_count() == 0;}

private:

	shared_refer m_refer;
};

