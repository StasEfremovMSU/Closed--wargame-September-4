using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using System.IO;

public static class tbsave  {

	public static void MakeSave () {
            using (StreamWriter sw = new StreamWriter("map1.txt"))
            {
			for (int i = 0; i < tbdata.tablregiment.Count; i++)
                {
                    sw.WriteLine( mystr(i) );
                }   
            }
	}

    static string mystr(int i)
    {
        string x = "";
		x += tbdata.tablregiment[i].command;
        x += ",";
		x += tbdata.tablregiment[i].number;
        x += ",";
		x += tbdata.tablregiment[i].gameObject.transform.position.x * 10;
        x += ",";
		x += tbdata.tablregiment[i].gameObject.transform.position.y;
        x += ",";
		x += tbdata.tablregiment[i].gameObject.transform.position.z * 10;
        x += ",";
		float a = tbdata.tablregiment[i].gameObject.transform.rotation.eulerAngles.y;
        a = a * 3.1415f / 180;
        if (a > 6.2830)
        {
            a = a % 6.283f;
        }
        if (a < 0)
        {
            while (a < 0)
            {
                a += 6.283f;
            }
        }
        x += a;
        x += ",";
		x += tbdata.tablregiment[i].countsol;
        x += ",";
		x += tbdata.tablregiment[i].fline;
        x += ",";
		x += tbdata.tablregiment[i].buf;
        return x;
    }
}
