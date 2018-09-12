using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using System.IO;
using System;


public class temm
{
    public string[] x;

    public temm()
    {
        x = new string[10];
    }

    public void  put (string [] y )
    {
        x = y;
    }

}

static public class LoadTxt {

    /// <summary>
    ///  Загружает файл со списком, выдает контейнер с элементами
    /// </summary>
    /// <param name="Name"></param>
    
    static public int number;

    static public List<temm> loadTxt(string Name)
    {

        /// Количество строк
        int q = 0;
        using (StreamReader reader1 = File.OpenText(Name))
        {

            string h = null;
            while ((h = reader1.ReadLine()) != null)
            {
                if (h[0] != '!')
                    q++;
            }
        }
        number = q;

        List<temm> res = new List<temm>(q);

        using (StreamReader reader1 = File.OpenText(Name))
        {
            string s = null;
            while ((s = reader1.ReadLine()) != null)
            {
                if (s[0] != '!')
                {
                    temm temp = new temm();
                    temp.put(s.Split(','));
                    res.Add(temp);
                }
            }
        }
        return res;



    }
    

}
