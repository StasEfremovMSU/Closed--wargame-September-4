using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using System;
using System.IO;

public class tbmain : MonoBehaviour {

    List<temm> data;
    int command;
    public GameObject pref;
    bool flag;
    bool flag1;

	void Start () {
		tbdata.temp = new List<tbregiment>();
		tbdata.tablregiment = new List<tbregiment>();
        data = new List<temm>();
        load();
        makeInst();
        flag = false;
        flag1 = false;
		AI.Start ();
		Debug.Log (AI.Gen_stat.Count);
		for (int i = 0; i < AI.Gen_stat.Count; i++) {
			AI.Gen_stat [i].makeGroupPoints ();
		}
	}

    public void FixedUpdate()
    {
		/// Не можем в один кадр создать объекты из прфаба и сразу на него повесить параметры, поэтому ждем следкадр
        if (flag == false)
        {
            flag = true;
        }
        else
        {
            if (flag1 == false)
            {
                flag1 = true;
                OtlInst(1);
            }
        }
    }

    public void makeInst()
    {
        int temp = 0;
        for (int i = 0; i < tbdata.temp.Count; i++)
        {
			float a = tbdata.temp [temp].angle;
			GameObject t = Instantiate(pref, tbdata.temp[temp].position,  Quaternion.AngleAxis(a * 180 / 3.1415f, new Vector3(0, 1, 0)));
			tbdata.tablregiment.Add(t.GetComponent<tbregiment>());
            temp ++;
        }
    }

    public void OtlInst(int com)
    {
		for (int k = 0; k < tbdata.tablregiment.Count;k++)
        {
			tbdata.tablregiment[k].angle =  tbdata.temp[k].angle;
			tbdata.tablregiment[k].countsol = tbdata.temp[k].countsol;
			tbdata.tablregiment[k].number = tbdata.temp[k].number;
			tbdata.tablregiment[k].gameObject.GetComponent<tbregiment>().number = tbdata.temp[k].number;
			tbdata.tablregiment[k].command = tbdata.temp[k].command;
			tbdata.tablregiment[k].fline = tbdata.temp[k].fline;
			tbdata.tablregiment[k].buf = tbdata.temp[k].buf;
			tbdata.tablregiment[k].fline_dist = tbdata.temp[k].fline_dist;
			tbdata.tablregiment[k].gameObject.transform.localScale = new Vector3( tbdata.temp[k].fline * AI.Scale * tbdata.temp[k].fline_dist,0.3f,  1);
			tbdata.tablregiment[k].gameObject.transform.position = tbdata.temp [k].position;
			if (tbdata.tablregiment[k].command != com)
                {
				tbdata.tablregiment[k].gameObject.SetActive(false);
                }
			tbdata.tablregiment[k].gameObject.GetComponent<tbregiment> ().command = tbdata.temp [k].command;
        }
    }
	
	
	public void load () {
        /// Поместили данные в сисок string.
        List<temm> data = LoadTxt.loadTxt("map1.txt");
        /// Количество полков команды, которую рассаматриваем.
        int com_length = NumCommandReg(1, command);
        for (int i = 0; i < data.Count; i++)
        {
			tbregiment temp = new tbregiment();
            temp.make(data[i]);
            tbdata.temp.Add(temp);
        }
	}

    /// <summary>
    ///  Количество полков одной команды
    /// </summary>
    /// <returns></returns>
    int NumCommandReg(int n, int command)
    {
        int count = 0;
        for (int t = 0; t < data.Count; t++)
        {
            if (int.Parse(data[t].x[n]) == command )
            {
                count++;
            }
        }
        return count;
    }

    void writeintxt()
    {
    }
}
