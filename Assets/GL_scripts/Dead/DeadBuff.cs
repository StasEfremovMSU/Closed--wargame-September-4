using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public static class DeadBuff  {

    static public List<DeathBuf> deathBuf;

    static public int t0;
    static public int t1;




    static public void mySort ()
    {

        deathBuf.Sort( delegate (DeathBuf x, DeathBuf y)
                {
                    
                    if (x.time < y.time)  return -1;
                    if (x.time > y.time) return 1;

                    return 0;
                });


    }

    static public void put(DeathBuf x)
    {
        // Делаем так, чтобы не повторялось
        bool y = false;
        for (int t = 0; t < deathBuf.Count; t++)
        {
            if (x.solNum == deathBuf[t].solNum && x.regNum == deathBuf[t].regNum)
            {
                y = true;
                break;
            }
        }

        if (y == false )  deathBuf.Add(x);
    }

    static public void Delete()
    {
        deathBuf.Clear();
    }

}



public struct DeathBuf
{
    public float time;
    public int solNum;
    public int regNum;

    public void newSolNum(int t)
    {
        solNum = solNum +1 ;
    }

    public DeathBuf(int reg , int sol , float z)
    {
        time = z;
        solNum = sol;
        regNum = reg;
    }
}
